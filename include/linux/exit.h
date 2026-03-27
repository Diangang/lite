#ifndef LINUX_EXIT_H
#define LINUX_EXIT_H

#include <stdint.h>

enum {
    TASK_EXIT_NORMAL = 0,
    TASK_EXIT_EXCEPTION = 1,
    TASK_EXIT_PAGEFAULT = 2,
    TASK_EXIT_SIGNAL = 3
};

void do_exit(int code);
void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1);

void sys_exit(int code);
int sys_waitpid(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1);
int sys_kill(uint32_t id, int sig);

#endif
