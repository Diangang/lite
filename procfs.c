#include "procfs.h"
#include "libc.h"
#include "task.h"
#include "timer.h"
#include "isr.h"
#include "kheap.h"

static struct dirent proc_dirent;
static fs_node_t proc_root;
static fs_node_t proc_tasks;
static fs_node_t proc_sched;
static fs_node_t proc_irq;
static fs_node_t proc_maps;

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
    return NULL;
}

fs_node_t *procfs_init(void)
{
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

    return &proc_root;
}
