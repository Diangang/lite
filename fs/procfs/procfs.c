#include "file.h"
#include "procfs.h"
#include "libc.h"
#include "init.h"
#include "task.h"
#include "timer.h"
#include "isr.h"
#include "kheap.h"
#include "pmm.h"
#include "vmm.h"

static struct dirent proc_dirent;
// Note: proc_root is dynamically allocated in init_procfs now
static struct inode proc_tasks;
static struct inode proc_sched;
static struct inode proc_irq;
static struct inode proc_maps;
static struct inode proc_meminfo;
static struct inode proc_cow;
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

static int parse_u32(const char *s, uint32_t *out);

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

static void buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    itoa((int)v, 10, tmp);
    buf_append(buf, off, cap, tmp);
}

static uint32_t proc_read_mounts(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[1024];
    uint32_t n = 0;

    for (struct vfsmount *m = vfs_get_mounts(); m; m = m->next) {
        if (!m->sb || !m->sb->name || !m->path)
            continue;
        buf_append(tmp, &n, sizeof(tmp), m->sb->name);
        buf_append(tmp, &n, sizeof(tmp), " ");
        buf_append(tmp, &n, sizeof(tmp), m->path);
        buf_append(tmp, &n, sizeof(tmp), "\n");
    }

    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_tasks(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[4096];
    uint32_t n = task_dump_tasks(tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_sched(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
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
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_irq(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
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
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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

static uint32_t proc_read_meminfo(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
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
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_cow(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[128];
    uint32_t off = 0;
    uint32_t faults = 0;
    uint32_t copies = 0;
    vmm_get_cow_stats(&faults, &copies);
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

static uint32_t proc_read_pid_stat(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_stat_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_cmdline(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = node->impl;
    uint32_t n = task_dump_cmdline_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_status(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = node->impl;
    uint32_t n = task_dump_status_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_cwd(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = node->impl;
    uint32_t n = task_dump_cwd_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_fd(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t slot = (node->impl >> 16) & 0xFFFF;
    uint32_t fd = node->impl & 0xFFFF;
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

static uint32_t proc_read_pid_maps(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[2048];
    uint32_t pid = node->impl;
    if (pid == 0xFFFFFFFF) pid = task_get_current_id();
    uint32_t n = task_dump_maps_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct dirent *proc_pid_fd_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    uint32_t slot = node->impl;
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
                itoa((int)fd, 10, e->dirent.name);
                e->dirent.ino = e->fd_files[fd].i_ino;
                return &e->dirent;
            }
        index--;
    }
    return NULL;
}

static struct inode *proc_pid_fd_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    uint32_t slot = node->impl;
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

static struct dirent *proc_pid_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    uint32_t slot = node->impl;
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

static struct inode *proc_pid_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    uint32_t slot = node->impl;
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

static struct file_operations proc_pid_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_pid_readdir,
    .finddir = proc_pid_finddir,
    .ioctl = NULL
};

static struct file_operations proc_pid_maps_ops = {
    .read = proc_read_pid_maps,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_stat_ops = {
    .read = proc_read_pid_stat,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_cmdline_ops = {
    .read = proc_read_pid_cmdline,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_status_ops = {
    .read = proc_read_pid_status,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_cwd_ops = {
    .read = proc_read_pid_cwd,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_pid_fd_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = proc_pid_fd_readdir,
    .finddir = proc_pid_fd_finddir,
    .ioctl = NULL
};

static struct file_operations proc_pid_fd_ops = {
    .read = proc_read_pid_fd,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

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
            e->cwd.flags = FS_FILE;
            e->cwd.i_ino = 0x5100 + i;
            e->cwd.i_size = 256;
            e->cwd.f_ops = &proc_pid_cwd_ops;
            e->cwd.impl = pid;
            e->cwd.uid = 0;
            e->cwd.gid = 0;
            e->cwd.i_mode = 0444;

            memset(&e->fd_dir, 0, sizeof(e->fd_dir));
            e->fd_dir.flags = FS_DIRECTORY;
            e->fd_dir.i_ino = 0x6000 + i;
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

static struct dirent *proc_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(proc_dirent.name, "tasks");
        proc_dirent.ino = proc_tasks.i_ino;
        return &proc_dirent;
    }
    if (index == 1) {
        strcpy(proc_dirent.name, "sched");
        proc_dirent.ino = proc_sched.i_ino;
        return &proc_dirent;
    }
    if (index == 2) {
        strcpy(proc_dirent.name, "irq");
        proc_dirent.ino = proc_irq.i_ino;
        return &proc_dirent;
    }
    if (index == 3) {
        strcpy(proc_dirent.name, "maps");
        proc_dirent.ino = proc_maps.i_ino;
        return &proc_dirent;
    }
    if (index == 4) {
        strcpy(proc_dirent.name, "meminfo");
        proc_dirent.ino = proc_meminfo.i_ino;
        return &proc_dirent;
    }
    if (index == 5) {
        strcpy(proc_dirent.name, "cow");
        proc_dirent.ino = proc_cow.i_ino;
        return &proc_dirent;
    }
    if (index == 6) {
        strcpy(proc_dirent.name, "mounts");
        proc_dirent.ino = proc_mounts.i_ino;
        return &proc_dirent;
    }
    if (index == 7) {
        strcpy(proc_dirent.name, "self");
        proc_dirent.ino = 0x1000;
        return &proc_dirent;
    }
    return NULL;
}

static struct inode *proc_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "tasks"))
        return &proc_tasks;
    if (!strcmp(name, "sched"))
        return &proc_sched;
    if (!strcmp(name, "irq"))
        return &proc_irq;
    if (!strcmp(name, "maps"))
        return &proc_maps;
    if (!strcmp(name, "meminfo"))
        return &proc_meminfo;
    if (!strcmp(name, "cow"))
        return &proc_cow;
    if (!strcmp(name, "mounts"))
        return &proc_mounts;
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
    .finddir = proc_finddir,
    .ioctl = NULL
};

static struct file_operations proc_tasks_ops = {
    .read = proc_read_tasks,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_sched_ops = {
    .read = proc_read_sched,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_irq_ops = {
    .read = proc_read_irq,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_maps_ops = {
    .read = proc_read_maps,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_meminfo_ops = {
    .read = proc_read_meminfo,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_cow_ops = {
    .read = proc_read_cow,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations proc_mounts_ops = {
    .read = proc_read_mounts,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

void init_procfs(void)
{
    vfs_mount_fs("/proc", "proc");
}

static int proc_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    memset(proc_pids, 0, sizeof(proc_pids));
    proc_get_pid_dir(0xFFFFFFFF);

    struct inode *proc_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!proc_root)
        return -1;

    memset(proc_root, 0, sizeof(struct inode));
    proc_root->flags = FS_DIRECTORY;
    proc_root->i_ino = 1;
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

    memset(&proc_cow, 0, sizeof(proc_cow));
    proc_cow.flags = FS_FILE;
    proc_cow.i_ino = 7;
    proc_cow.i_size = 128;
    proc_cow.f_ops = &proc_cow_ops;
    proc_cow.uid = 0;
    proc_cow.gid = 0;
    proc_cow.i_mode = 0444;

    memset(&proc_mounts, 0, sizeof(proc_mounts));
    proc_mounts.flags = FS_FILE;
    proc_mounts.i_ino = 8;
    proc_mounts.i_size = 1024;
    proc_mounts.f_ops = &proc_mounts_ops;
    proc_mounts.uid = 0;
    proc_mounts.gid = 0;
    proc_mounts.i_mode = 0444;

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

static int init_proc_fs(void)
{
    register_filesystem(&proc_fs_type);
    printf("proc filesystem registered.\n");
    return 0;
}
module_init(init_proc_fs);
