#ifndef LINUX_SIGNAL_H
#define LINUX_SIGNAL_H

#include <stdint.h>

struct task_struct;

enum {
    SIGHUP = 1,
    SIGINT = 2,
    SIGQUIT = 3,
    SIGILL = 4,
    SIGTRAP = 5,
    SIGABRT = 6,
    SIGBUS = 7,
    SIGFPE = 8,
    SIGKILL = 9,
    SIGUSR1 = 10,
    SIGSEGV = 11,
    SIGUSR2 = 12,
    SIGPIPE = 13,
    SIGALRM = 14,
    SIGTERM = 15,
    SIGCHLD = 17
};

int sys_kill(uint32_t id, int sig);
void signal_notify_exit(struct task_struct *child);

#endif
