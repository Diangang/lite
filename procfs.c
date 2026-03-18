#include "procfs.h"
#include "libc.h"
#include "task.h"
#include "timer.h"
#include "isr.h"
#include "kheap.h"
#include "pmm.h"

static struct dirent proc_dirent;
static fs_node_t proc_root;
static fs_node_t proc_tasks;
static fs_node_t proc_sched;
static fs_node_t proc_irq;
static fs_node_t proc_maps;
static fs_node_t proc_meminfo;

typedef struct {
    int used;
    uint32_t pid;
    fs_node_t dir;
    fs_node_t maps;
    fs_node_t stat;
    struct dirent dirent;
} proc_pid_entry_t;

enum { PROC_PID_MAX = 16 };
static proc_pid_entry_t proc_pids[PROC_PID_MAX];

static void buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || cap == 0 || !s) return;
    while (*s && *off + 1 < cap) {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

static void buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    itoa((int)v, 10, tmp);
    buf_append(buf, off, cap, tmp);
}

static uint32_t proc_read_tasks(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[4096];
    uint32_t n = task_dump_tasks(tmp, sizeof(tmp));
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_sched(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t ticks = timer_get_ticks();
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
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_irq(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t n = 0;
    buf_append(tmp, &n, sizeof(tmp), "irq0=");
    buf_append_u32(tmp, &n, sizeof(tmp), isr_get_count(IRQ0));
    buf_append(tmp, &n, sizeof(tmp), "\nirq1=");
    buf_append_u32(tmp, &n, sizeof(tmp), isr_get_count(IRQ1));
    buf_append(tmp, &n, sizeof(tmp), "\nirq4=");
    buf_append_u32(tmp, &n, sizeof(tmp), isr_get_count(36));
    buf_append(tmp, &n, sizeof(tmp), "\nsyscall128=");
    buf_append_u32(tmp, &n, sizeof(tmp), isr_get_count(128));
    buf_append(tmp, &n, sizeof(tmp), "\n");
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_maps(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[2048];
    uint32_t n = task_dump_maps(tmp, sizeof(tmp));
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_meminfo(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[256];
    uint32_t off = 0;
    uint32_t total_kb = pmm_get_total_kb();
    uint32_t free_kb = pmm_get_free_kb();
    buf_append(tmp, &off, sizeof(tmp), "MemTotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nMemFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), free_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\n");
    if (off < sizeof(tmp)) tmp[off] = 0;
    if (offset >= off) return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_stat(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node) return 0;
    static char tmp[256];
    uint32_t pid = node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_stat_pid(pid, tmp, sizeof(tmp));
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_maps(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node) return 0;
    static char tmp[2048];
    uint32_t pid = node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_maps_pid(pid, tmp, sizeof(tmp));
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct dirent *proc_pid_readdir(fs_node_t *node, uint32_t index)
{
    if (!node) return NULL;
    uint32_t slot = node->impl;
    if (slot >= PROC_PID_MAX) return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used) return NULL;
    if (index == 0) {
        strcpy(e->dirent.name, "maps");
        e->dirent.ino = e->maps.inode;
        return &e->dirent;
    }
    if (index == 1) {
        strcpy(e->dirent.name, "stat");
        e->dirent.ino = e->stat.inode;
        return &e->dirent;
    }
    return NULL;
}

static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name)
{
    if (!node || !name) return NULL;
    uint32_t slot = node->impl;
    if (slot >= PROC_PID_MAX) return NULL;
    proc_pid_entry_t *e = &proc_pids[slot];
    if (!e->used) return NULL;
    if (!strcmp(name, "maps")) return &e->maps;
    if (!strcmp(name, "stat")) return &e->stat;
    return NULL;
}

static int parse_u32(const char *s, uint32_t *out)
{
    if (!s || !out) return -1;
    if (*s == 0) return -1;
    uint32_t v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        uint32_t d = (uint32_t)(*p - '0');
        uint32_t nv = v * 10 + d;
        if (nv < v) return -1;
        v = nv;
    }
    *out = v;
    return 0;
}

static fs_node_t *proc_get_pid_dir(uint32_t pid)
{
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (proc_pids[i].used && proc_pids[i].pid == pid) return &proc_pids[i].dir;
    }
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (!proc_pids[i].used) {
            proc_pid_entry_t *e = &proc_pids[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->pid = pid;

            memset(&e->dir, 0, sizeof(e->dir));
            if (pid == 0xFFFFFFFF) {
                strcpy(e->dir.name, "self");
            } else {
                itoa((int)pid, 10, e->dir.name);
            }
            e->dir.flags = FS_DIRECTORY;
            e->dir.inode = 0x1000 + i;
            e->dir.readdir = &proc_pid_readdir;
            e->dir.finddir = &proc_pid_finddir;
            e->dir.impl = i;

            memset(&e->maps, 0, sizeof(e->maps));
            strcpy(e->maps.name, "maps");
            e->maps.flags = FS_FILE;
            e->maps.inode = 0x2000 + i;
            e->maps.length = 2048;
            e->maps.read = &proc_read_pid_maps;
            e->maps.impl = pid;

            memset(&e->stat, 0, sizeof(e->stat));
            strcpy(e->stat.name, "stat");
            e->stat.flags = FS_FILE;
            e->stat.inode = 0x3000 + i;
            e->stat.length = 256;
            e->stat.read = &proc_read_pid_stat;
            e->stat.impl = pid;

            return &e->dir;
        }
    }
    return NULL;
}

static struct dirent *proc_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(proc_dirent.name, "tasks");
        proc_dirent.ino = proc_tasks.inode;
        return &proc_dirent;
    }
    if (index == 1) {
        strcpy(proc_dirent.name, "sched");
        proc_dirent.ino = proc_sched.inode;
        return &proc_dirent;
    }
    if (index == 2) {
        strcpy(proc_dirent.name, "irq");
        proc_dirent.ino = proc_irq.inode;
        return &proc_dirent;
    }
    if (index == 3) {
        strcpy(proc_dirent.name, "maps");
        proc_dirent.ino = proc_maps.inode;
        return &proc_dirent;
    }
    if (index == 4) {
        strcpy(proc_dirent.name, "meminfo");
        proc_dirent.ino = proc_meminfo.inode;
        return &proc_dirent;
    }
    if (index == 5) {
        strcpy(proc_dirent.name, "self");
        proc_dirent.ino = 0x1000;
        return &proc_dirent;
    }
    return NULL;
}

static fs_node_t *proc_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "tasks")) return &proc_tasks;
    if (!strcmp(name, "sched")) return &proc_sched;
    if (!strcmp(name, "irq")) return &proc_irq;
    if (!strcmp(name, "maps")) return &proc_maps;
    if (!strcmp(name, "meminfo")) return &proc_meminfo;
    if (!strcmp(name, "self")) return proc_get_pid_dir(0xFFFFFFFF);
    uint32_t pid = 0;
    if (parse_u32(name, &pid) == 0) {
        return proc_get_pid_dir(pid);
    }
    return NULL;
}

fs_node_t *procfs_init(void)
{
    memset(proc_pids, 0, sizeof(proc_pids));
    proc_get_pid_dir(0xFFFFFFFF);

    memset(&proc_root, 0, sizeof(proc_root));
    strcpy(proc_root.name, "proc");
    proc_root.flags = FS_DIRECTORY;
    proc_root.readdir = &proc_readdir;
    proc_root.finddir = &proc_finddir;

    memset(&proc_tasks, 0, sizeof(proc_tasks));
    strcpy(proc_tasks.name, "tasks");
    proc_tasks.flags = FS_FILE;
    proc_tasks.inode = 1;
    proc_tasks.length = 4096;
    proc_tasks.read = &proc_read_tasks;

    memset(&proc_sched, 0, sizeof(proc_sched));
    strcpy(proc_sched.name, "sched");
    proc_sched.flags = FS_FILE;
    proc_sched.inode = 2;
    proc_sched.length = 1024;
    proc_sched.read = &proc_read_sched;

    memset(&proc_irq, 0, sizeof(proc_irq));
    strcpy(proc_irq.name, "irq");
    proc_irq.flags = FS_FILE;
    proc_irq.inode = 3;
    proc_irq.length = 1024;
    proc_irq.read = &proc_read_irq;

    memset(&proc_maps, 0, sizeof(proc_maps));
    strcpy(proc_maps.name, "maps");
    proc_maps.flags = FS_FILE;
    proc_maps.inode = 4;
    proc_maps.length = 2048;
    proc_maps.read = &proc_read_maps;

    memset(&proc_meminfo, 0, sizeof(proc_meminfo));
    strcpy(proc_meminfo.name, "meminfo");
    proc_meminfo.flags = FS_FILE;
    proc_meminfo.inode = 5;
    proc_meminfo.length = 256;
    proc_meminfo.read = &proc_read_meminfo;

    return &proc_root;
}
