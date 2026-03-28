#ifndef LINUX_EXIT_H
#define LINUX_EXIT_H

#include <stdint.h>

struct task_struct;

enum {
    TASK_EXIT_NORMAL = 0,
    TASK_EXIT_EXCEPTION = 1,
    TASK_EXIT_PAGEFAULT = 2,
    TASK_EXIT_SIGNAL = 3
};

void do_exit(int code);
void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1);
void task_destroy(struct task_struct *prev, struct task_struct *task);

void sys_exit(int code);

#endif
