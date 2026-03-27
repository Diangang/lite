#include "linux/sched.h"
#include "linux/fdtable.h"
#include "linux/file.h"

void files_init(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        task->files.fd[i].used = 0;
        task->files.fd[i].file = NULL;
    }
}

void files_close_all(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (task->files.fd[i].used) {
            if (task->files.fd[i].file)
                file_close(task->files.fd[i].file);
            task->files.fd[i].used = 0;
            task->files.fd[i].file = NULL;
        }
    }
}

void files_clone(struct task_struct *dst, struct task_struct *src)
{
    if (!dst || !src)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (src->files.fd[i].used && src->files.fd[i].file) {
            dst->files.fd[i].used = 1;
            dst->files.fd[i].file = file_dup(src->files.fd[i].file);
        }
    }
}

int get_unused_fd(struct file *file)
{
    if (!current || !file)
        return -1;
    for (int i = 3; i < TASK_FD_MAX; i++) {
        if (!current->files.fd[i].used) {
            current->files.fd[i].used = 1;
            current->files.fd[i].file = file;
            return i;
        }
    }
    return -1;
}

struct file *fget(int fd)
{
    if (!current)
        return NULL;
    if (fd < 0 || fd >= TASK_FD_MAX)
        return NULL;
    if (!current->files.fd[fd].used)
        return NULL;
    if (!current->files.fd[fd].file)
        return NULL;
    return current->files.fd[fd].file;
}

int close_fd(int fd)
{
    if (fd < 3)
        return -1;
    struct file *f = fget(fd);
    if (!f)
        return -1;
    file_close(f);
    current->files.fd[fd].used = 0;
    current->files.fd[fd].file = NULL;
    return 0;
}

void install_stdio(struct inode *console)
{
    if (!current)
        return;
    if (!console)
        return;

    current->files.fd[0].used = 1;
    current->files.fd[0].file = file_open_node(console, 0);

    current->files.fd[1].used = 1;
    current->files.fd[1].file = file_open_node(console, 0);

    current->files.fd[2].used = 1;
    current->files.fd[2].file = file_open_node(console, 0);
}
