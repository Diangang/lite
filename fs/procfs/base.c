#include "linux/sched.h"
#include "linux/pid.h"
#include "internal.h"
#include "linux/fs.h"
#include "linux/file.h"

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
