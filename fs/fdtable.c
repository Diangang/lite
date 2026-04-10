#include "linux/sched.h"
#include "linux/fdtable.h"
#include "linux/file.h"

/* files_init: Initialize files. */
void files_init(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        task->files.fdt.used[i] = 0;
        task->files.fdt.fd[i] = NULL;
    }
}

/* files_close_all: Implement files close all. */
void files_close_all(struct task_struct *task)
{
    if (!task)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (task->files.fdt.used[i]) {
            if (task->files.fdt.fd[i])
                file_close(task->files.fdt.fd[i]);
            task->files.fdt.used[i] = 0;
            task->files.fdt.fd[i] = NULL;
        }
    }
}

/* files_clone: Implement files clone. */
void files_clone(struct task_struct *dst, struct task_struct *src)
{
    if (!dst || !src)
        return;
    for (int i = 0; i < TASK_FD_MAX; i++) {
        if (src->files.fdt.used[i] && src->files.fdt.fd[i]) {
            dst->files.fdt.used[i] = 1;
            dst->files.fdt.fd[i] = file_dup(src->files.fdt.fd[i]);
        }
    }
}

/* get_unused_fd: Get unused fd. */
int get_unused_fd(struct file *file)
{
    if (!current || !file)
        return -1;
    for (int i = 3; i < TASK_FD_MAX; i++) {
        if (!current->files.fdt.used[i]) {
            current->files.fdt.used[i] = 1;
            current->files.fdt.fd[i] = file;
            return i;
        }
    }
    return -1;
}

/* fget: Implement fget. */
struct file *fget(int fd)
{
    if (!current)
        return NULL;
    if (fd < 0 || fd >= TASK_FD_MAX)
        return NULL;
    if (!current->files.fdt.used[fd])
        return NULL;
    if (!current->files.fdt.fd[fd])
        return NULL;
    return current->files.fdt.fd[fd];
}

/* close_fd: Close fd. */
int close_fd(int fd)
{
    if (fd < 3)
        return -1;
    struct file *f = fget(fd);
    if (!f)
        return -1;
    file_close(f);
    current->files.fdt.used[fd] = 0;
    current->files.fdt.fd[fd] = NULL;
    return 0;
}

/* install_stdio: Implement install stdio. */
void install_stdio(struct inode *console)
{
    if (!current)
        return;
    if (!console)
        return;

    current->files.fdt.used[0] = 1;
    current->files.fdt.fd[0] = file_open_node(console, 0);

    current->files.fdt.used[1] = 1;
    current->files.fdt.fd[1] = file_open_node(console, 0);

    current->files.fdt.used[2] = 1;
    current->files.fdt.fd[2] = file_open_node(console, 0);
}
