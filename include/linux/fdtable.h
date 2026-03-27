#ifndef LINUX_FDTABLE_H
#define LINUX_FDTABLE_H

#include <stdint.h>

struct file;
struct inode;
struct task_struct;

struct fd_struct {
    int used;
    struct file *file;
};

enum { TASK_FD_MAX = 32 };

struct files_struct {
    struct fd_struct fd[TASK_FD_MAX];
};

void files_init(struct task_struct *task);
void files_close_all(struct task_struct *task);
void files_clone(struct task_struct *dst, struct task_struct *src);

int get_unused_fd(struct file *file);
struct file *fget(int fd);
int close_fd(int fd);
void install_stdio(struct inode *console);

#endif
