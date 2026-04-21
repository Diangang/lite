// fs/proc/generic.c - Lite procfs misc root entries (Linux mapping: fs/proc/generic.c)

#include <stdint.h>

#include "asm/setup.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

#include "linux/bootmem.h"
#include "linux/blkdev.h"
#include "linux/fs.h"
#include "linux/init.h"
#include "linux/interrupt.h"
#include "asm/pgtable.h"
#include "linux/mmzone.h"
#include "linux/gfp.h"
#include "linux/sched.h"
#include "linux/time.h"
#include "linux/mmzone.h"
#include "linux/writeback.h"

#include "base.h"
#include "internal.h"

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
    if (size > remain)
        size = remain;
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
    if (size > remain)
        size = remain;
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

    if (!devices_kset)
        return 0;
    struct kobject *kobj;
    list_for_each_entry(kobj, &devices_kset->list, entry) {
        struct device *dev = container_of(kobj, struct device, kobj);
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

static struct file_operations proc_maps_ops = {
    .read = proc_read_maps,
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

static struct inode proc_tasks;
static struct inode proc_sched;
static struct inode proc_maps;
static struct inode proc_iomem;
static struct inode proc_cow;
static struct inode proc_pfault;
static struct inode proc_vmscan;
static struct inode proc_writeback;
static struct inode proc_pagecache;
static struct inode proc_blockstats;
static struct inode proc_diskstats;
static struct inode proc_mounts;

void proc_generic_init(void)
{
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

    memset(&proc_maps, 0, sizeof(proc_maps));
    proc_maps.flags = FS_FILE;
    proc_maps.i_ino = 5;
    proc_maps.i_size = 2048;
    proc_maps.f_ops = &proc_maps_ops;
    proc_maps.uid = 0;
    proc_maps.gid = 0;
    proc_maps.i_mode = 0444;

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
    proc_register_root_child("maps", &proc_maps);
    proc_register_root_child("iomem", &proc_iomem);
    proc_register_root_child("cow", &proc_cow);
    proc_register_root_child("pfault", &proc_pfault);
    proc_register_root_child("vmscan", &proc_vmscan);
    proc_register_root_child("writeback", &proc_writeback);
    proc_register_root_child("pagecache", &proc_pagecache);
    proc_register_root_child("blockstats", &proc_blockstats);
    proc_register_root_child("diskstats", &proc_diskstats);
    proc_register_root_child("mounts", &proc_mounts);
}
