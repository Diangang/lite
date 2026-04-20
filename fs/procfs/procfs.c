#include "linux/libc.h"
#include "linux/fs.h"
#include "linux/file.h"
#include "linux/init.h"
#include "linux/sched.h"
#include "linux/fdtable.h"
#include "internal.h"
#include "linux/time.h"
#include "linux/interrupt.h"
#include "linux/slab.h"
#include "linux/page_alloc.h"
#include "linux/mmzone.h"
#include "linux/vmscan.h"
#include "linux/pagemap.h"
#include "linux/blkdev.h"
#include "linux/buffer_head.h"
#include "linux/device.h"
#include "base.h"
#include "linux/bootmem.h"
#include "linux/memlayout.h"
#include "linux/vsprintf.h"
#include "asm/pgtable.h"

static struct dirent proc_dirent;
// proc_root is allocated when procfs superblock is built.
static struct inode proc_tasks;
static struct inode proc_sched;
static struct inode proc_irq;
static struct inode proc_maps;
static struct inode proc_meminfo;
static struct inode proc_iomem;
static struct inode proc_cow;
static struct inode proc_pfault;
static struct inode proc_vmscan;
static struct inode proc_writeback;
static struct inode proc_pagecache;
static struct inode proc_blockstats;
static struct inode proc_diskstats;
static struct inode proc_mounts;

typedef struct {
    int used;
    uint32_t pid;
    struct inode dir;
    struct inode maps;
    struct inode stat;
    struct inode cmdline;
    struct inode status;
    struct inode cwd;
    struct inode fd_dir;
    struct inode fd_files[TASK_FD_MAX];
    struct dirent dirent;
} proc_pid_entry_t;

enum { PROC_PID_MAX = 16 };
static proc_pid_entry_t proc_pids[PROC_PID_MAX];

static struct inode_operations proc_pid_dir_iops;
static struct inode_operations proc_pid_fd_dir_iops;
static struct inode_operations procfs_dir_iops;

/*
 * Minimal proc_dir_entry-style model (Linux mapping):
 * - Linux procfs uses proc_dir_entry objects to build a dynamic tree.
 * - Lite keeps the implementation small; use a fixed root child table and
 *   keep per-pid entries bounded (PROC_PID_MAX).
 */
struct proc_dir_entry {
    const char *name;
    struct inode *inode;
};

static struct proc_dir_entry proc_root_children[16];
static uint32_t proc_root_children_nr;

static void proc_register_root_child(const char *name, struct inode *inode)
{
    if (!name || !name[0] || !inode)
        return;
    if (proc_root_children_nr >= (uint32_t)(sizeof(proc_root_children) / sizeof(proc_root_children[0])))
        return;
    proc_root_children[proc_root_children_nr].name = name;
    proc_root_children[proc_root_children_nr].inode = inode;
    proc_root_children_nr++;
}

static struct inode *proc_lookup_root_child(const char *name)
{
    for (uint32_t i = 0; i < proc_root_children_nr; i++) {
        if (!proc_root_children[i].name)
            continue;
        if (!strcmp(proc_root_children[i].name, name))
            return proc_root_children[i].inode;
    }
    return NULL;
}

static struct dirent *proc_fill_dirent(const char *name, uint32_t ino)
{
    if (!name || !name[0])
        return NULL;
    strcpy(proc_dirent.name, name);
    proc_dirent.ino = ino;
    return &proc_dirent;
}

/* parse_u32: Parse u32. */
static int parse_u32(const char *s, uint32_t *out)
{
    if (!s || !out)
        return -1;
    if (*s == 0)
        return -1;
    uint32_t v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        uint32_t d = (uint32_t)(*p - '0');
        uint32_t nv = v * 10 + d;
        if (nv < v)
            return -1;
        v = nv;
    }
    *out = v;
    return 0;
}

/* buf_append: Implement buf append. */
static void buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || cap == 0 || !s)
        return;
    while (*s && *off + 1 < cap) {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

/* buf_append_u32: Implement buf append u32. */
static void buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%u", v);
    buf_append(buf, off, cap, tmp);
}

/* buf_append_hex: Implement buf append hex. */
static void buf_append_hex(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%x", v);
    buf_append(buf, off, cap, "0x");
    buf_append(buf, off, cap, tmp);
}

struct proc_seq_state {
    uint32_t pos;
    uint32_t start;
    uint32_t end;
    uint8_t *out;
    uint32_t out_cap;
    uint32_t out_off;
    int full;
};

static void proc_seq_emit_bytes(struct proc_seq_state *st, const char *s, uint32_t n)
{
    if (!st || !s || n == 0 || st->full)
        return;

    uint32_t seg_start = st->pos;
    uint32_t seg_end = st->pos + n;
    st->pos = seg_end;

    if (seg_end <= st->start)
        return;
    if (seg_start >= st->end)
        return;

    uint32_t copy_from = 0;
    if (st->start > seg_start)
        copy_from = st->start - seg_start;
    uint32_t avail = n - copy_from;

    uint32_t copy_len = avail;
    uint32_t max_to_end = st->end - (seg_start + copy_from);
    if (copy_len > max_to_end)
        copy_len = max_to_end;
    if (copy_len > st->out_cap - st->out_off)
        copy_len = st->out_cap - st->out_off;

    if (copy_len == 0)
        return;
    memcpy(st->out + st->out_off, s + copy_from, copy_len);
    st->out_off += copy_len;
    if (st->out_off >= st->out_cap)
        st->full = 1;
}

static void proc_seq_emit_str(struct proc_seq_state *st, const char *s)
{
    if (!s)
        return;
    proc_seq_emit_bytes(st, s, (uint32_t)strlen(s));
}

static void proc_seq_emit_u32(struct proc_seq_state *st, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%u", v);
    proc_seq_emit_str(st, tmp);
}

/* proc_read_mounts: Implement proc read mounts. */
static uint32_t proc_read_mounts(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    if (!buffer || size == 0)
        return 0;
    struct proc_seq_state st = {
        .pos = 0,
        .start = offset,
        .end = offset + size,
        .out = buffer,
        .out_cap = size,
        .out_off = 0,
        .full = 0,
    };

    for (struct vfsmount *m = vfs_get_mounts(); m && !st.full; m = m->next) {
        if (!m->sb || !m->sb->name || !m->path)
            continue;
        proc_seq_emit_str(&st, m->sb->name);
        proc_seq_emit_str(&st, " ");
        proc_seq_emit_str(&st, m->path);
        proc_seq_emit_str(&st, "\n");
    }
    return st.out_off;
}

/* proc_read_tasks: Implement proc read tasks. */
static uint32_t proc_read_tasks(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    if (!buffer || size == 0)
        return 0;
    struct proc_seq_state st = {
        .pos = 0,
        .start = offset,
        .end = offset + size,
        .out = buffer,
        .out_cap = size,
        .out_off = 0,
        .full = 0,
    };

    proc_seq_emit_str(&st, "PID   STATE     WAKE    CURRENT  NAME\n");
    if (!list_empty(&task_list_head)) {
        struct task_struct *task;
        list_for_each_entry(task, &task_list_head, tasks) {
            if (st.full)
                break;
            const char *state = "RUNNABLE";
            if (task->state == TASK_SLEEPING)
                state = "SLEEPING";
            else if (task->state == TASK_BLOCKED)
                state = "BLOCKED";
            else if (task->state == TASK_ZOMBIE)
                state = "ZOMBIE";
            const char *name = task->comm[0] ? task->comm : "-";
            proc_seq_emit_u32(&st, task->pid);
            proc_seq_emit_str(&st, "    ");
            proc_seq_emit_str(&st, state);
            proc_seq_emit_str(&st, "  ");
            proc_seq_emit_u32(&st, task->wake_jiffies);
            proc_seq_emit_str(&st, "    ");
            proc_seq_emit_str(&st, task == current ? "yes" : "no");
            proc_seq_emit_str(&st, "     ");
            proc_seq_emit_str(&st, name);
            proc_seq_emit_str(&st, "\n");
        }
    }
    return st.out_off;
}

/* proc_read_sched: Implement proc read sched. */
static uint32_t proc_read_sched(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t ticks = time_get_jiffies();
    uint32_t switches = task_get_switch_count();
    uint32_t cur = task_get_current_id();
    uint32_t n = 0;
    buf_append(tmp, &n, sizeof(tmp), "ticks=");
    buf_append_u32(tmp, &n, sizeof(tmp), ticks);
    buf_append(tmp, &n, sizeof(tmp), "\nswitches=");
    buf_append_u32(tmp, &n, sizeof(tmp), switches);
    buf_append(tmp, &n, sizeof(tmp), "\ncurrent=");
    buf_append_u32(tmp, &n, sizeof(tmp), cur);
    buf_append(tmp, &n, sizeof(tmp), "\n");
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_irq: Implement proc read IRQ. */
static uint32_t proc_read_irq(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t n = 0;
    buf_append(tmp, &n, sizeof(tmp), "irq0=");
    buf_append_u32(tmp, &n, sizeof(tmp), irq_get_count(IRQ_TIMER));
    buf_append(tmp, &n, sizeof(tmp), "\nirq1=");
    buf_append_u32(tmp, &n, sizeof(tmp), irq_get_count(IRQ_KEYBOARD));
    buf_append(tmp, &n, sizeof(tmp), "\nirq4=");
    buf_append_u32(tmp, &n, sizeof(tmp), irq_get_count(IRQ_COM1));
    buf_append(tmp, &n, sizeof(tmp), "\nsyscall128=");
    buf_append_u32(tmp, &n, sizeof(tmp), isr_get_count(128));
    buf_append(tmp, &n, sizeof(tmp), "\n");
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_maps: Implement proc read maps. */
static uint32_t proc_read_maps(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[2048];
    uint32_t n = task_dump_maps(tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_meminfo: Implement proc read meminfo. */
static uint32_t proc_read_meminfo(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1536];
    uint32_t off = 0;
    uint32_t total_kb = (uint32_t)(totalram_pages() * (PAGE_SIZE / 1024));
    uint32_t free_kb = (uint32_t)(nr_free_pages() * (PAGE_SIZE / 1024));
    uint32_t min_kb = 0;
    uint32_t low_kb = 0;
    uint32_t high_kb = 0;
    uint32_t zone_dma_total_kb = contig_page_data.zone_dma.managed_pages * (PAGE_SIZE / 1024);
    uint32_t zone_dma_free_kb = (uint32_t)(zone_free_pages(&contig_page_data.zone_dma) * (PAGE_SIZE / 1024));
    uint32_t zone_normal_total_kb = contig_page_data.zone_normal.managed_pages * (PAGE_SIZE / 1024);
    uint32_t zone_normal_free_kb = (uint32_t)(zone_free_pages(&contig_page_data.zone_normal) * (PAGE_SIZE / 1024));
    uint32_t e820_ram_kb = bootmem_ram_kb();
    uint32_t e820_reserved_kb = bootmem_reserved_kb();
    uint32_t lowmem_end_kb = bootmem_lowmem_end() / 1024;
    uint32_t lowmem_phys_end_kb = memlayout_lowmem_phys_end() / 1024;
    uint32_t direct_map_start_kb = memlayout_directmap_start() / 1024;
    uint32_t direct_map_end_kb = memlayout_directmap_end() / 1024;
    uint32_t vmalloc_start_kb = memlayout_vmalloc_start() / 1024;
    uint32_t vmalloc_end_kb = memlayout_vmalloc_end() / 1024;
    uint32_t fixaddr_start_kb = memlayout_fixaddr_start() / 1024;
    if (contig_page_data.zone_dma.spanned_pages) {
        min_kb += contig_page_data.zone_dma.watermark[WMARK_MIN] * (PAGE_SIZE / 1024);
        low_kb += contig_page_data.zone_dma.watermark[WMARK_LOW] * (PAGE_SIZE / 1024);
        high_kb += contig_page_data.zone_dma.watermark[WMARK_HIGH] * (PAGE_SIZE / 1024);
    }
    if (contig_page_data.zone_normal.spanned_pages) {
        min_kb += contig_page_data.zone_normal.watermark[WMARK_MIN] * (PAGE_SIZE / 1024);
        low_kb += contig_page_data.zone_normal.watermark[WMARK_LOW] * (PAGE_SIZE / 1024);
        high_kb += contig_page_data.zone_normal.watermark[WMARK_HIGH] * (PAGE_SIZE / 1024);
    }
    buf_append(tmp, &off, sizeof(tmp), "MemTotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nMemFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), free_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nE820Ram: ");
    buf_append_u32(tmp, &off, sizeof(tmp), e820_ram_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nE820Reserved: ");
    buf_append_u32(tmp, &off, sizeof(tmp), e820_reserved_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nLowMemEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), lowmem_end_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nLowMemPhysEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), lowmem_phys_end_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nDirectMapStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), direct_map_start_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nDirectMapEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), direct_map_end_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nVmallocStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), vmalloc_start_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nVmallocEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), vmalloc_end_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nFixaddrStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), fixaddr_start_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nMinFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), min_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nLowFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), low_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nHighFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), high_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nKswapdWakeups: ");
    buf_append_u32(tmp, &off, sizeof(tmp), kswapd_wakeup_count());
    buf_append(tmp, &off, sizeof(tmp), "\nZoneDMATotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_dma_total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneDMAFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_dma_free_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneNormalTotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_normal_total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneNormalFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_normal_free_kb);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp)) tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* e820_type_name: Implement e820 type name. */
static const char *e820_type_name(uint32_t type)
{
    switch (type) {
        case MULTIBOOT_MEMORY_AVAILABLE:
            return "System RAM";
        case MULTIBOOT_MEMORY_RESERVED:
            return "reserved";
        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
            return "ACPI Reclaimable";
        case MULTIBOOT_MEMORY_NVS:
            return "ACPI NVS";
        case MULTIBOOT_MEMORY_BADRAM:
            return "Bad RAM";
        default:
            return "reserved";
    }
}

/* proc_read_iomem: Implement proc read iomem. */
static uint32_t proc_read_iomem(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t off = 0;

    uint32_t n = bootmem_e820_entries();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t base = 0;
        uint32_t len = 0;
        uint32_t type = 0;
        if (bootmem_e820_get(i, &base, &len, &type) != 0)
            continue;
        if (len == 0)
            continue;
        uint32_t end = base + len - 1;
        buf_append_hex(tmp, &off, sizeof(tmp), base);
        buf_append(tmp, &off, sizeof(tmp), "-");
        buf_append_hex(tmp, &off, sizeof(tmp), end);
        buf_append(tmp, &off, sizeof(tmp), " : ");
        buf_append(tmp, &off, sizeof(tmp), e820_type_name(type));
        buf_append(tmp, &off, sizeof(tmp), "\n");
        if (off + 64 >= sizeof(tmp))
            break;
    }

    uint32_t kstart = bootmem_kernel_phys_start();
    uint32_t kend = bootmem_kernel_phys_end();
    if (kend > kstart) {
        buf_append_hex(tmp, &off, sizeof(tmp), kstart);
        buf_append(tmp, &off, sizeof(tmp), "-");
        buf_append_hex(tmp, &off, sizeof(tmp), kend - 1);
        buf_append(tmp, &off, sizeof(tmp), " : Kernel\n");
    }

    uint32_t mc = bootmem_module_count();
    for (uint32_t i = 0; i < mc; i++) {
        uint32_t ms = 0;
        uint32_t me = 0;
        if (bootmem_module_get(i, &ms, &me) != 0)
            continue;
        if (me <= ms)
            continue;
        buf_append_hex(tmp, &off, sizeof(tmp), ms);
        buf_append(tmp, &off, sizeof(tmp), "-");
        buf_append_hex(tmp, &off, sizeof(tmp), me - 1);
        buf_append(tmp, &off, sizeof(tmp), " : initramfs\n");
        if (off + 64 >= sizeof(tmp))
            break;
    }

    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_cow: Implement proc read copy-on-write. */
static uint32_t proc_read_cow(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[160];
    uint32_t off = 0;
    uint32_t faults = 0;
    uint32_t copies = 0;
    get_cow_stats(&faults, &copies);
    buf_append(tmp, &off, sizeof(tmp), "faults=");
    buf_append_u32(tmp, &off, sizeof(tmp), faults);
    buf_append(tmp, &off, sizeof(tmp), "\ncopies=");
    buf_append_u32(tmp, &off, sizeof(tmp), copies);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp)) tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pfault: Implement proc read pfault. */
static uint32_t proc_read_pfault(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[256];
    uint32_t off = 0;
    uint32_t total = 0, present = 0, not_present = 0, write = 0, user = 0, kernel = 0;
    uint32_t reserved = 0, prot = 0, null = 0, kernel_addr = 0, out_of_range = 0;
    get_pf_stats(&total, &present, &not_present, &write, &user, &kernel, &reserved, &prot, &null, &kernel_addr, &out_of_range);
    buf_append(tmp, &off, sizeof(tmp), "total=");
    buf_append_u32(tmp, &off, sizeof(tmp), total);
    buf_append(tmp, &off, sizeof(tmp), "\npresent=");
    buf_append_u32(tmp, &off, sizeof(tmp), present);
    buf_append(tmp, &off, sizeof(tmp), "\nnot_present=");
    buf_append_u32(tmp, &off, sizeof(tmp), not_present);
    buf_append(tmp, &off, sizeof(tmp), "\nwrite=");
    buf_append_u32(tmp, &off, sizeof(tmp), write);
    buf_append(tmp, &off, sizeof(tmp), "\nuser=");
    buf_append_u32(tmp, &off, sizeof(tmp), user);
    buf_append(tmp, &off, sizeof(tmp), "\nkernel=");
    buf_append_u32(tmp, &off, sizeof(tmp), kernel);
    buf_append(tmp, &off, sizeof(tmp), "\nreserved=");
    buf_append_u32(tmp, &off, sizeof(tmp), reserved);
    buf_append(tmp, &off, sizeof(tmp), "\nprot=");
    buf_append_u32(tmp, &off, sizeof(tmp), prot);
    buf_append(tmp, &off, sizeof(tmp), "\nnull=");
    buf_append_u32(tmp, &off, sizeof(tmp), null);
    buf_append(tmp, &off, sizeof(tmp), "\nkernel_addr=");
    buf_append_u32(tmp, &off, sizeof(tmp), kernel_addr);
    buf_append(tmp, &off, sizeof(tmp), "\nout_of_range=");
    buf_append_u32(tmp, &off, sizeof(tmp), out_of_range);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_vmscan: Implement proc read vmscan. */
static uint32_t proc_read_vmscan(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[128];
    uint32_t off = 0;
    uint32_t wakeups = kswapd_wakeup_count();
    uint32_t tries = kswapd_try_count();
    uint32_t reclaims = kswapd_reclaim_count();
    uint32_t anon = kswapd_anon_reclaim_count();
    uint32_t file = kswapd_file_reclaim_count();
    buf_append(tmp, &off, sizeof(tmp), "kswapd_wakeups=");
    buf_append_u32(tmp, &off, sizeof(tmp), wakeups);
    buf_append(tmp, &off, sizeof(tmp), "\nkswapd_tries=");
    buf_append_u32(tmp, &off, sizeof(tmp), tries);
    buf_append(tmp, &off, sizeof(tmp), "\nkswapd_reclaims=");
    buf_append_u32(tmp, &off, sizeof(tmp), reclaims);
    buf_append(tmp, &off, sizeof(tmp), "\nkswapd_anon_reclaims=");
    buf_append_u32(tmp, &off, sizeof(tmp), anon);
    buf_append(tmp, &off, sizeof(tmp), "\nkswapd_file_reclaims=");
    buf_append_u32(tmp, &off, sizeof(tmp), file);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_writeback: Implement proc read writeback. */
static uint32_t proc_read_writeback(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[128];
    uint32_t off = 0;
    uint32_t dirty = 0, cleaned = 0, discarded = 0, throttled = 0;
    get_writeback_stats(&dirty, &cleaned, &discarded, &throttled);
    buf_append(tmp, &off, sizeof(tmp), "dirty=");
    buf_append_u32(tmp, &off, sizeof(tmp), dirty);
    buf_append(tmp, &off, sizeof(tmp), "\ncleaned=");
    buf_append_u32(tmp, &off, sizeof(tmp), cleaned);
    buf_append(tmp, &off, sizeof(tmp), "\ndiscarded=");
    buf_append_u32(tmp, &off, sizeof(tmp), discarded);
    buf_append(tmp, &off, sizeof(tmp), "\nthrottled=");
    buf_append_u32(tmp, &off, sizeof(tmp), throttled);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_blockstats: Implement proc read blockstats. */
static uint32_t proc_read_blockstats(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[160];
    uint32_t off = 0;
    uint32_t reads = 0, writes = 0, bytes_read = 0, bytes_written = 0;
    get_block_stats(&reads, &writes, &bytes_read, &bytes_written);
    buf_append(tmp, &off, sizeof(tmp), "reads=");
    buf_append_u32(tmp, &off, sizeof(tmp), reads);
    buf_append(tmp, &off, sizeof(tmp), "\nwrites=");
    buf_append_u32(tmp, &off, sizeof(tmp), writes);
    buf_append(tmp, &off, sizeof(tmp), "\nbytes_read=");
    buf_append_u32(tmp, &off, sizeof(tmp), bytes_read);
    buf_append(tmp, &off, sizeof(tmp), "\nbytes_written=");
    buf_append_u32(tmp, &off, sizeof(tmp), bytes_written);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_diskstats: Implement proc read diskstats. */
static uint32_t proc_read_diskstats(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[768];
    uint32_t off = 0;

    uint32_t n = registered_device_count();
    for (uint32_t i = 0; i < n; i++) {
        struct device *dev = registered_device_at(i);
        if (!dev || dev->type != &disk_type)
            continue;
        struct gendisk *disk = gendisk_from_dev(dev);
        struct block_device *bdev = disk ? bdget_disk(disk, 0) : NULL;
        if (!disk || !bdev)
            continue;

        buf_append(tmp, &off, sizeof(tmp), disk->disk_name);
        buf_append(tmp, &off, sizeof(tmp), " reads=");
        buf_append_u32(tmp, &off, sizeof(tmp), bdev->reads);
        buf_append(tmp, &off, sizeof(tmp), " writes=");
        buf_append_u32(tmp, &off, sizeof(tmp), bdev->writes);
        buf_append(tmp, &off, sizeof(tmp), " bytes_read=");
        buf_append_u32(tmp, &off, sizeof(tmp), bdev->bytes_read);
        buf_append(tmp, &off, sizeof(tmp), " bytes_written=");
        buf_append_u32(tmp, &off, sizeof(tmp), bdev->bytes_written);
        buf_append(tmp, &off, sizeof(tmp), "\n");
        bdput(bdev);
    }

    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_write_writeback: Implement proc write writeback. */
static uint32_t proc_write_writeback(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)node;
    (void)offset;
    (void)buffer;
    writeback_flush_all();
    sync_dirty_buffers_all();
    return size;
}

/* proc_read_pagecache: Implement proc read page cache. */
static uint32_t proc_read_pagecache(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[128];
    uint32_t off = 0;
    uint32_t hits = 0, misses = 0;
    get_pagecache_stats(&hits, &misses);
    buf_append(tmp, &off, sizeof(tmp), "hits=");
    buf_append_u32(tmp, &off, sizeof(tmp), hits);
    buf_append(tmp, &off, sizeof(tmp), "\nmisses=");
    buf_append_u32(tmp, &off, sizeof(tmp), misses);
    buf_append(tmp, &off, sizeof(tmp), "\n");
    if (off < sizeof(tmp))
        tmp[off] = 0;
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_write_vmscan: Implement proc write vmscan. */
static uint32_t proc_write_vmscan(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)node;
    (void)offset;
    (void)buffer;
    writeback_flush_all();
    if (contig_page_data.zone_dma.spanned_pages)
        try_to_free_pages(&contig_page_data.zone_dma, 0);
    if (contig_page_data.zone_normal.spanned_pages)
        try_to_free_pages(&contig_page_data.zone_normal, 0);
    return size;
}

/* proc_read_pid_stat: Implement proc read pid stat. */
static uint32_t proc_read_pid_stat(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = (uint32_t)node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_stat_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pid_cmdline: Implement proc read pid cmdline. */
static uint32_t proc_read_pid_cmdline(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = (uint32_t)node->impl;
    uint32_t n = task_dump_cmdline_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pid_status: Implement proc read pid status. */
static uint32_t proc_read_pid_status(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = (uint32_t)node->impl;
    uint32_t n = task_dump_status_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pid_cwd: Implement proc read pid cwd. */
static uint32_t proc_read_pid_cwd(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = (uint32_t)node->impl;
    if (pid == 0xFFFFFFFF)
        pid = task_get_current_id();
    uint32_t n = task_dump_cwd_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pid_fd: Implement proc read pid fd. */
static uint32_t proc_read_pid_fd(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t impl = (uint32_t)node->impl;
    uint32_t slot = (impl >> 16) & 0xFFFF;
    uint32_t fd = impl & 0xFFFF;
    if (slot >= PROC_PID_MAX)
        return 0;
    uint32_t pid = proc_pids[slot].pid;
    uint32_t n = task_dump_fd_pid(pid, fd, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_read_pid_maps: Implement proc read pid maps. */
static uint32_t proc_read_pid_maps(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[2048];
    uint32_t pid = (uint32_t)node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_maps_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* proc_pid_fd_readdir: Implement proc pid fd readdir. */
static struct dirent *proc_pid_fd_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    uint32_t slot = (uint32_t)node->impl;
    if (slot >= PROC_PID_MAX)
        return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used)
        return NULL;

    uint32_t pid = e->pid;
    static char tmp[64];
    for (uint32_t fd = 0; fd < TASK_FD_MAX; fd++) {
        uint32_t n = task_dump_fd_pid(pid, fd, tmp, sizeof(tmp));
        if (n == 0)
            continue;
        if (index == 0) {
            snprintf(e->dirent.name, sizeof(e->dirent.name), "%u", fd);
            e->dirent.ino = e->fd_files[fd].i_ino;
            return &e->dirent;
        }
        index--;
    }
    return NULL;
}

/* proc_pid_fd_finddir: Implement proc pid fd finddir. */
static struct inode *proc_pid_fd_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    uint32_t slot = (uint32_t)node->impl;
    if (slot >= PROC_PID_MAX)
        return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used)
        return NULL;

    uint32_t fd = 0;
    if (parse_u32(name, &fd) != 0)
        return NULL;
    if (fd >= TASK_FD_MAX)
        return NULL;
    char tmp[64];
    if (task_dump_fd_pid(e->pid, fd, tmp, sizeof(tmp)) == 0)
        return NULL;
    return &e->fd_files[fd];
}

/* proc_pid_readdir: Implement proc pid readdir. */
static struct dirent *proc_pid_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    uint32_t slot = (uint32_t)node->impl;
    if (slot >= PROC_PID_MAX)
        return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used)
        return NULL;
    if (index == 0) {
        strcpy(e->dirent.name, "maps");
        e->dirent.ino = e->maps.i_ino;
        return &e->dirent;
    }
    if (index == 1) {
        strcpy(e->dirent.name, "stat");
        e->dirent.ino = e->stat.i_ino;
        return &e->dirent;
    }
    if (index == 2) {
        strcpy(e->dirent.name, "cmdline");
        e->dirent.ino = e->cmdline.i_ino;
        return &e->dirent;
    }
    if (index == 3) {
        strcpy(e->dirent.name, "status");
        e->dirent.ino = e->status.i_ino;
        return &e->dirent;
    }
    if (index == 4) {
        strcpy(e->dirent.name, "cwd");
        e->dirent.ino = e->cwd.i_ino;
        return &e->dirent;
    }
    if (index == 5) {
        strcpy(e->dirent.name, "fd");
        e->dirent.ino = e->fd_dir.i_ino;
        return &e->dirent;
    }
    return NULL;
}

/* proc_pid_finddir: Implement proc pid finddir. */
static struct inode *proc_pid_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    uint32_t slot = (uint32_t)node->impl;
    if (slot >= PROC_PID_MAX)
        return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used)
        return NULL;
    if (!strcmp(name, "maps"))
        return &e->maps;
    if (!strcmp(name, "stat"))
        return &e->stat;
    if (!strcmp(name, "cmdline"))
        return &e->cmdline;
    if (!strcmp(name, "status"))
        return &e->status;
    if (!strcmp(name, "cwd"))
        return &e->cwd;
    if (!strcmp(name, "fd"))
        return &e->fd_dir;
    return NULL;
}

static struct file_operations proc_pid_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_pid_readdir,
    .ioctl = NULL
};

static struct inode_operations proc_pid_dir_iops = {
    .lookup = proc_pid_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations proc_pid_maps_ops = {
    .read = proc_read_pid_maps,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_stat_ops = {
    .read = proc_read_pid_stat,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_cmdline_ops = {
    .read = proc_read_pid_cmdline,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_status_ops = {
    .read = proc_read_pid_status,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_cwd_ops = {
    .read = proc_read_pid_cwd,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_fd_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_pid_fd_readdir,
    .ioctl = NULL
};

static struct inode_operations proc_pid_fd_dir_iops = {
    .lookup = proc_pid_fd_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations proc_pid_fd_ops = {
    .read = proc_read_pid_fd,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

/* proc_get_pid_dir: Implement proc get pid dir. */
struct inode *proc_get_pid_dir(uint32_t pid)
{
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (proc_pids[i].used && proc_pids[i].pid == pid)
            return &proc_pids[i].dir;
    }
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (!proc_pids[i].used) {
            proc_pid_entry_t *e = &proc_pids[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->pid = pid;

            memset(&e->dir, 0, sizeof(e->dir));
            e->dir.flags = FS_DIRECTORY;
            e->dir.i_ino = 0x1000 + i;
            e->dir.i_op = &proc_pid_dir_iops;
            e->dir.f_ops = &proc_pid_dir_ops;
            e->dir.impl = i;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.i_mode = 0555;

            memset(&e->maps, 0, sizeof(e->maps));
            e->maps.flags = FS_FILE;
            e->maps.i_ino = 0x2000 + i;
            e->maps.i_size = 2048;
            e->maps.f_ops = &proc_pid_maps_ops;
            e->maps.impl = pid;
            e->maps.uid = 0;
            e->maps.gid = 0;
            e->maps.i_mode = 0444;

            memset(&e->stat, 0, sizeof(e->stat));
            e->stat.flags = FS_FILE;
            e->stat.i_ino = 0x3000 + i;
            e->stat.i_size = 256;
            e->stat.f_ops = &proc_pid_stat_ops;
            e->stat.impl = pid;
            e->stat.uid = 0;
            e->stat.gid = 0;
            e->stat.i_mode = 0444;

            memset(&e->cmdline, 0, sizeof(e->cmdline));
            e->cmdline.flags = FS_FILE;
            e->cmdline.i_ino = 0x4000 + i;
            e->cmdline.i_size = 256;
            e->cmdline.f_ops = &proc_pid_cmdline_ops;
            e->cmdline.impl = pid;
            e->cmdline.uid = 0;
            e->cmdline.gid = 0;
            e->cmdline.i_mode = 0444;

            memset(&e->status, 0, sizeof(e->status));
            e->status.flags = FS_FILE;
            e->status.i_ino = 0x5000 + i;
            e->status.i_size = 256;
            e->status.f_ops = &proc_pid_status_ops;
            e->status.impl = pid;
            e->status.uid = 0;
            e->status.gid = 0;
            e->status.i_mode = 0444;

            memset(&e->cwd, 0, sizeof(e->cwd));
            /* Linux mapping: /proc/<pid>/cwd is a symlink. */
            e->cwd.flags = FS_SYMLINK;
            e->cwd.i_ino = 0x5100 + i;
            e->cwd.i_size = 0;
            e->cwd.f_ops = &proc_pid_cwd_ops;
            e->cwd.impl = pid;
            e->cwd.uid = 0;
            e->cwd.gid = 0;
            e->cwd.i_mode = 0777;

            memset(&e->fd_dir, 0, sizeof(e->fd_dir));
            e->fd_dir.flags = FS_DIRECTORY;
            e->fd_dir.i_ino = 0x6000 + i;
            e->fd_dir.i_op = &proc_pid_fd_dir_iops;
            e->fd_dir.f_ops = &proc_pid_fd_dir_ops;
            e->fd_dir.impl = i;
            e->fd_dir.uid = 0;
            e->fd_dir.gid = 0;
            e->fd_dir.i_mode = 0555;

            for (uint32_t fd = 0; fd < TASK_FD_MAX; fd++) {
                memset(&e->fd_files[fd], 0, sizeof(struct inode));
                e->fd_files[fd].flags = FS_FILE;
                e->fd_files[fd].i_ino = 0x7000 + i * TASK_FD_MAX + fd;
                e->fd_files[fd].i_size = 256;
                e->fd_files[fd].f_ops = &proc_pid_fd_ops;
                e->fd_files[fd].impl = (i << 16) | fd;
                e->fd_files[fd].uid = 0;
                e->fd_files[fd].gid = 0;
                e->fd_files[fd].i_mode = 0444;
            }

            return &e->dir;
        }
    }
    return NULL;
}

/* proc_readdir: Implement proc readdir. */
static struct dirent *proc_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index < proc_root_children_nr) {
        const char *name = proc_root_children[index].name;
        struct inode *inode = proc_root_children[index].inode;
        return proc_fill_dirent(name, inode ? inode->i_ino : 0);
    }

    /* After fixed children, list bounded per-pid entries if present. */
    uint32_t pid_index = index - proc_root_children_nr;
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (!proc_pids[i].used)
            continue;
        if (pid_index == 0) {
            char name[16];
            snprintf(name, sizeof(name), "%u", proc_pids[i].pid);
            return proc_fill_dirent(name, proc_pids[i].dir.i_ino);
        }
        pid_index--;
    }

    return NULL;
}

/* proc_finddir: Implement proc finddir. */
static struct inode *proc_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct inode *fixed = proc_lookup_root_child(name);
    if (fixed)
        return fixed;
    if (!strcmp(name, "self"))
        return proc_get_pid_dir(0xFFFFFFFF);
    uint32_t pid = 0;
    if (parse_u32(name, &pid) == 0)
        return proc_get_pid_dir(pid);
    return NULL;
}

static struct file_operations procfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_readdir,
    .ioctl = NULL
};

static struct inode_operations procfs_dir_iops = {
    .lookup = proc_finddir,
    .create = NULL,
    .mkdir = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations proc_tasks_ops = {
    .read = proc_read_tasks,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_sched_ops = {
    .read = proc_read_sched,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_irq_ops = {
    .read = proc_read_irq,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_maps_ops = {
    .read = proc_read_maps,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_meminfo_ops = {
    .read = proc_read_meminfo,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_iomem_ops = {
    .read = proc_read_iomem,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_cow_ops = {
    .read = proc_read_cow,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pfault_ops = {
    .read = proc_read_pfault,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_vmscan_ops = {
    .read = proc_read_vmscan,
    .write = proc_write_vmscan,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_writeback_ops = {
    .read = proc_read_writeback,
    .write = proc_write_writeback,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pagecache_ops = {
    .read = proc_read_pagecache,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_blockstats_ops = {
    .read = proc_read_blockstats,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_diskstats_ops = {
    .read = proc_read_diskstats,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_mounts_ops = {
    .read = proc_read_mounts,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

/* proc_fill_super: Implement proc fill super. */
static int proc_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    memset(proc_pids, 0, sizeof(proc_pids));
    proc_get_pid_dir(0xFFFFFFFF);
    proc_root_children_nr = 0;

    struct inode *proc_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!proc_root)
        return -1;

    memset(proc_root, 0, sizeof(struct inode));
    proc_root->flags = FS_DIRECTORY;
    proc_root->i_ino = 1;
    proc_root->i_op = &procfs_dir_iops;
    proc_root->f_ops = &procfs_dir_ops;
    proc_root->uid = 0;
    proc_root->gid = 0;
    proc_root->i_mode = 0555;

    memset(&proc_tasks, 0, sizeof(proc_tasks));
    proc_tasks.flags = FS_FILE;
    proc_tasks.i_ino = 2;
    proc_tasks.i_size = 4096;
    proc_tasks.f_ops = &proc_tasks_ops;
    proc_tasks.uid = 0;
    proc_tasks.gid = 0;
    proc_tasks.i_mode = 0444;

    memset(&proc_sched, 0, sizeof(proc_sched));
    proc_sched.flags = FS_FILE;
    proc_sched.i_ino = 3;
    proc_sched.i_size = 1024;
    proc_sched.f_ops = &proc_sched_ops;
    proc_sched.uid = 0;
    proc_sched.gid = 0;
    proc_sched.i_mode = 0444;

    memset(&proc_irq, 0, sizeof(proc_irq));
    proc_irq.flags = FS_FILE;
    proc_irq.i_ino = 4;
    proc_irq.i_size = 1024;
    proc_irq.f_ops = &proc_irq_ops;
    proc_irq.uid = 0;
    proc_irq.gid = 0;
    proc_irq.i_mode = 0444;

    memset(&proc_maps, 0, sizeof(proc_maps));
    proc_maps.flags = FS_FILE;
    proc_maps.i_ino = 5;
    proc_maps.i_size = 2048;
    proc_maps.f_ops = &proc_maps_ops;
    proc_maps.uid = 0;
    proc_maps.gid = 0;
    proc_maps.i_mode = 0444;

    memset(&proc_meminfo, 0, sizeof(proc_meminfo));
    proc_meminfo.flags = FS_FILE;
    proc_meminfo.i_ino = 6;
    proc_meminfo.i_size = 256;
    proc_meminfo.f_ops = &proc_meminfo_ops;
    proc_meminfo.uid = 0;
    proc_meminfo.gid = 0;
    proc_meminfo.i_mode = 0444;

    memset(&proc_iomem, 0, sizeof(proc_iomem));
    proc_iomem.flags = FS_FILE;
    proc_iomem.i_ino = 9;
    proc_iomem.i_size = 1024;
    proc_iomem.f_ops = &proc_iomem_ops;
    proc_iomem.uid = 0;
    proc_iomem.gid = 0;
    proc_iomem.i_mode = 0444;

    memset(&proc_cow, 0, sizeof(proc_cow));
    proc_cow.flags = FS_FILE;
    proc_cow.i_ino = 7;
    proc_cow.i_size = 128;
    proc_cow.f_ops = &proc_cow_ops;
    proc_cow.uid = 0;
    proc_cow.gid = 0;
    proc_cow.i_mode = 0444;

    memset(&proc_pfault, 0, sizeof(proc_pfault));
    proc_pfault.flags = FS_FILE;
    proc_pfault.i_ino = 10;
    proc_pfault.i_size = 256;
    proc_pfault.f_ops = &proc_pfault_ops;
    proc_pfault.uid = 0;
    proc_pfault.gid = 0;
    proc_pfault.i_mode = 0444;

    memset(&proc_vmscan, 0, sizeof(proc_vmscan));
    proc_vmscan.flags = FS_FILE;
    proc_vmscan.i_ino = 11;
    proc_vmscan.i_size = 128;
    proc_vmscan.f_ops = &proc_vmscan_ops;
    proc_vmscan.uid = 0;
    proc_vmscan.gid = 0;
    proc_vmscan.i_mode = 0444;

    memset(&proc_writeback, 0, sizeof(proc_writeback));
    proc_writeback.flags = FS_FILE;
    proc_writeback.i_ino = 12;
    proc_writeback.i_size = 128;
    proc_writeback.f_ops = &proc_writeback_ops;
    proc_writeback.uid = 0;
    proc_writeback.gid = 0;
    proc_writeback.i_mode = 0444;

    memset(&proc_pagecache, 0, sizeof(proc_pagecache));
    proc_pagecache.flags = FS_FILE;
    proc_pagecache.i_ino = 13;
    proc_pagecache.i_size = 128;
    proc_pagecache.f_ops = &proc_pagecache_ops;
    proc_pagecache.uid = 0;
    proc_pagecache.gid = 0;
    proc_pagecache.i_mode = 0444;

    memset(&proc_blockstats, 0, sizeof(proc_blockstats));
    proc_blockstats.flags = FS_FILE;
    proc_blockstats.i_ino = 14;
    proc_blockstats.i_size = 160;
    proc_blockstats.f_ops = &proc_blockstats_ops;
    proc_blockstats.uid = 0;
    proc_blockstats.gid = 0;
    proc_blockstats.i_mode = 0444;

    memset(&proc_diskstats, 0, sizeof(proc_diskstats));
    proc_diskstats.flags = FS_FILE;
    proc_diskstats.i_ino = 15;
    proc_diskstats.i_size = 768;
    proc_diskstats.f_ops = &proc_diskstats_ops;
    proc_diskstats.uid = 0;
    proc_diskstats.gid = 0;
    proc_diskstats.i_mode = 0444;

    memset(&proc_mounts, 0, sizeof(proc_mounts));
    proc_mounts.flags = FS_FILE;
    proc_mounts.i_ino = 8;
    proc_mounts.i_size = 1024;
    proc_mounts.f_ops = &proc_mounts_ops;
    proc_mounts.uid = 0;
    proc_mounts.gid = 0;
    proc_mounts.i_mode = 0444;

    proc_register_root_child("tasks", &proc_tasks);
    proc_register_root_child("sched", &proc_sched);
    proc_register_root_child("irq", &proc_irq);
    proc_register_root_child("maps", &proc_maps);
    proc_register_root_child("meminfo", &proc_meminfo);
    proc_register_root_child("iomem", &proc_iomem);
    proc_register_root_child("cow", &proc_cow);
    proc_register_root_child("pfault", &proc_pfault);
    proc_register_root_child("vmscan", &proc_vmscan);
    proc_register_root_child("writeback", &proc_writeback);
    proc_register_root_child("pagecache", &proc_pagecache);
    proc_register_root_child("blockstats", &proc_blockstats);
    proc_register_root_child("diskstats", &proc_diskstats);
    proc_register_root_child("mounts", &proc_mounts);
    /* Keep self as a special dynamic entry. */
    proc_register_root_child("self", proc_get_pid_dir(0xFFFFFFFF));

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = proc_root;

    return 0;
}

static struct file_system_type proc_fs_type = {
    .name = "proc",
    .get_sb = vfs_get_sb_single,
    .fill_super = proc_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* proc_root_init: Register the proc filesystem. */
static int proc_root_init(void)
{
    register_filesystem(&proc_fs_type);
    printf("proc filesystem registered.\n");
    return 0;
}
fs_initcall(proc_root_init);
