#ifndef LINUX_KTHREAD_H
#define LINUX_KTHREAD_H

struct task_struct;

struct task_struct *kthread_run(int (*threadfn)(void *data), void *data, const char *name);

#endif
