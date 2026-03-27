#include "linux/sched.h"
#include "linux/pid.h"
#include "internal.h"
#include "linux/fs.h"
#include "linux/file.h"

const char *task_get_cwd(void)
{
    static char buf[256];
    if (!current || !current->fs.pwd)
        return "/";
    struct dentry *d = current->fs.pwd;
    if (!d->parent)
        return "/";

    char tmp[256];
    int pos = 255;
    tmp[pos] = 0;

    while (d && d->parent) {
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

uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_pid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    proc_buf_append(buf, &off, len, "/");
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_fd_pid(uint32_t pid, uint32_t fd, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_pid(pid);
    if (!t)
        return 0;
    if (fd >= TASK_FD_MAX)
        return 0;
    if (!t->files.fd[fd].used || !t->files.fd[fd].file || !t->files.fd[fd].file->dentry->inode)
        return 0;

    uint32_t off = 0;
    proc_buf_append(buf, &off, len, t->files.fd[fd].file->dentry->name);
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}
