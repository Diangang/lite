#include "linux/sched.h"
#include "linux/pid.h"
#include "internal.h"
#include "linux/fs.h"
#include "linux/file.h"
#include "linux/fdtable.h"

/* task_get_cwd: Implement task get cwd. */
const char *task_get_cwd(void)
{
    static char buf[256];
    if (!current || !current->fs.pwd)
        return "/";
    struct dentry *d = current->fs.pwd;
    if (!d->parent || d->parent == d)
        return "/";

    char tmp[256];
    int pos = 255;
    tmp[pos] = 0;

    while (d && d->parent && d->parent != d) {
        /* Mount topology: if this is a mounted root, hop to mountpoint and continue. */
        if (d->mount && d->mount->root == d && d->mount->mountpoint) {
            d = d->mount->mountpoint;
            continue;
        }
        int n = strlen(d->name);
        if (pos - n - 1 < 0)
            break;
        pos -= n;
        memcpy(tmp + pos, d->name, n);
        pos -= 1;
        tmp[pos] = '/';
        d = d->parent;
    }

    if (pos == 255)
        strcpy(buf, "/");
    else {
        int final_pos = pos;
        if (tmp[final_pos] == '/' && tmp[final_pos + 1] == '/')
            final_pos++;
        strcpy(buf, tmp + final_pos);
    }
    return buf;
}

/* task_dump_cwd_pid: Implement task dump cwd pid. */
uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t)
        return 0;

    struct dentry *d = t->fs.pwd;
    if (!d) {
        if (len > 1) {
            buf[0] = '/';
            buf[1] = 0;
        } else {
            buf[0] = 0;
        }
        return 1;
    }

    char tmp[256];
    int pos = 255;
    tmp[pos] = 0;

    if (!d->parent || d->parent == d) {
        if (len > 1) {
            buf[0] = '/';
            buf[1] = 0;
        } else {
            buf[0] = 0;
        }
        return 1;
    }

    while (d && d->parent && d->parent != d) {
        /* Mount topology: if this is a mounted root, hop to mountpoint and continue. */
        if (d->mount && d->mount->root == d && d->mount->mountpoint) {
            d = d->mount->mountpoint;
            continue;
        }
        int n = strlen(d->name);
        if (pos - n - 1 < 0)
            break;
        pos -= n;
        memcpy(tmp + pos, d->name, n);
        pos -= 1;
        tmp[pos] = '/';
        d = d->parent;
    }

    const char *src = "/";
    if (pos != 255) {
        int final_pos = pos;
        if (tmp[final_pos] == '/' && tmp[final_pos + 1] == '/')
            final_pos++;
        src = tmp + final_pos;
    }

    uint32_t n = (uint32_t)strlen(src);
    if (n + 1 > len)
        n = len - 1;
    memcpy(buf, src, n);
    buf[n] = 0;
    return n;
}

/* task_dump_fd_pid: Implement task dump fd pid. */
uint32_t task_dump_fd_pid(uint32_t pid, uint32_t fd, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t)
        return 0;
    if (fd >= TASK_FD_MAX)
        return 0;
    if (!t->files.fdt.used[fd] || !t->files.fdt.fd[fd] || !t->files.fdt.fd[fd]->dentry->inode)
        return 0;

    uint32_t off = 0;
    proc_buf_append(buf, &off, len, t->files.fdt.fd[fd]->dentry->name);
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

/*
 * Per-process /proc subtree (Linux mapping: linux2.6/fs/proc/base.c)
 */

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

static uint32_t proc_read_pid_stat(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[256];
    uint32_t pid = (uint32_t)node->impl;
    if (pid == 0xFFFFFFFF)
        pid = task_get_current_id();
    uint32_t n = task_dump_stat_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t proc_read_pid_maps(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node)
        return 0;
    static char tmp[2048];
    uint32_t pid = (uint32_t)node->impl;
    if (pid == 0xFFFFFFFF)
        pid = task_get_current_id();
    uint32_t n = task_dump_maps_pid(pid, tmp, sizeof(tmp));
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

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

void proc_pid_init(void)
{
    memset(proc_pids, 0, sizeof(proc_pids));
    (void)proc_get_pid_dir(0xFFFFFFFF);
}

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

struct inode *proc_pid_lookup(const char *name)
{
    if (!name)
        return NULL;
    if (!strcmp(name, "self"))
        return proc_get_pid_dir(0xFFFFFFFF);
    uint32_t pid = 0;
    if (parse_u32(name, &pid) == 0)
        return proc_get_pid_dir(pid);
    return NULL;
}

struct dirent *proc_pid_readdir_root(uint32_t index)
{
    uint32_t pid_index = index;
    for (uint32_t i = 0; i < PROC_PID_MAX; i++) {
        if (!proc_pids[i].used)
            continue;
        if (pid_index == 0) {
            static struct dirent dent;
            snprintf(dent.name, sizeof(dent.name), "%u", proc_pids[i].pid);
            dent.ino = proc_pids[i].dir.i_ino;
            return &dent;
        }
        pid_index--;
    }
    return NULL;
}

