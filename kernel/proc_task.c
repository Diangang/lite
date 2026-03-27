#include "task_internal.h"
#include "file.h"
#include "libc.h"
#include "fs.h"

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
    char tmp[16];
    itoa((int)v, 10, tmp);
    buf_append(buf, off, cap, tmp);
}

static void buf_append_hex(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 16, tmp);
    buf_append(buf, off, cap, "0x");
    buf_append(buf, off, cap, tmp);
}

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

struct dentry *task_get_cwd_dentry(void)
{
    if (!current)
        return NULL;
    return current->fs.pwd;
}

struct dentry *task_get_root_dentry(void)
{
    if (!current)
        return NULL;
    return current->fs.root;
}

int task_set_cwd_dentry(struct dentry *d)
{
    if (!current || !d)
        return -1;
    if (current->fs.pwd)
        vfs_dentry_put(current->fs.pwd);
    d->refcount++;
    current->fs.pwd = d;
    return 0;
}

uint32_t task_dump_tasks(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;

    uint32_t off = 0;
    buf_append(buf, &off, len, "PID   STATE     WAKE    CURRENT  NAME\n");
    struct task_struct *task = task_head;
    do {
        const char *state = "RUNNABLE";
        if (task->state == 1)
            state = "SLEEPING";
        else if (task->state == 2)
            state = "BLOCKED";
        else if (task->state == 3)
            state = "ZOMBIE";
        const char *name = task->comm[0] ? task->comm : "-";
        buf_append_u32(buf, &off, len, task->pid);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, state);
        buf_append(buf, &off, len, "  ");
        buf_append_u32(buf, &off, len, task->wake_jiffies);
        buf_append(buf, &off, len, "    ");
        buf_append(buf, &off, len, task == current ? "yes" : "no");
        buf_append(buf, &off, len, "     ");
        buf_append(buf, &off, len, name);
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        task = task->next;
    } while (task && task != task_head);

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_maps(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!current || !current->mm)
        return 0;

    uint32_t off = 0;
    struct vm_area_struct *v = current->mm->mmap;
    while (v) {
        buf_append_hex(buf, &off, len, v->vm_start);
        buf_append(buf, &off, len, "-");
        buf_append_hex(buf, &off, len, v->vm_end);
        buf_append(buf, &off, len, " ");
        buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_maps_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!task_head)
        return 0;

    struct task_struct *t = task_find_by_pid(pid);
    if (!t || !t->mm)
        return 0;

    uint32_t off = 0;
    struct vm_area_struct *v = t->mm->mmap;
    while (v) {
        buf_append_hex(buf, &off, len, v->vm_start);
        buf_append(buf, &off, len, "-");
        buf_append_hex(buf, &off, len, v->vm_end);
        buf_append(buf, &off, len, " ");
        buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_stat_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = task_find_by_pid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    const char *name = t->comm[0] ? t->comm : "-";
    const char *st = "R";
    if (t->state == 1)
        st = "S";
    else if (t->state == 2)
        st = "D";
    else if (t->state == 3)
        st = "Z";
    buf_append_u32(buf, &off, len, t->pid);
    buf_append(buf, &off, len, " (");
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, ") ");
    buf_append(buf, &off, len, st);
    buf_append(buf, &off, len, " ");
    buf_append_u32(buf, &off, len, t->wake_jiffies);
    buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_cmdline_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = task_find_by_pid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    const char *name = t->comm[0] ? t->comm : "-";
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_status_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = task_find_by_pid(pid);
    if (!t)
        return 0;

    const char *name = t->comm[0] ? t->comm : "-";
    const char *state = "RUNNABLE";
    if (t->state == 1)
        state = "SLEEPING";
    else if (t->state == 2)
        state = "BLOCKED";
    else if (t->state == 3)
        state = "ZOMBIE";

    uint32_t off = 0;
    buf_append(buf, &off, len, "Name:\t");
    buf_append(buf, &off, len, name);
    buf_append(buf, &off, len, "\nState:\t");
    buf_append(buf, &off, len, state);
    buf_append(buf, &off, len, "\nPid:\t");
    buf_append_u32(buf, &off, len, t->pid);
    buf_append(buf, &off, len, "\nType:\t");
    buf_append(buf, &off, len, t->mm ? "user" : "kthread");
    buf_append(buf, &off, len, "\nCwd:\t");
    buf_append(buf, &off, len, "/");
    buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = task_find_by_pid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    buf_append(buf, &off, len, "/");
    buf_append(buf, &off, len, "\n");

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

    struct task_struct *t = task_find_by_pid(pid);
    if (!t)
        return 0;
    if (fd >= TASK_FD_MAX)
        return 0;
    if (!t->files.fd[fd].used || !t->files.fd[fd].file || !t->files.fd[fd].file->dentry->inode)
        return 0;

    uint32_t off = 0;
    buf_append(buf, &off, len, t->files.fd[fd].file->dentry->name);
    buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}
