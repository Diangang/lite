#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "isr.h"

enum {
    SYS_WRITE = 0,
    SYS_YIELD = 1,
    SYS_SLEEP = 2,
    SYS_EXIT = 3,
    SYS_READ = 4,
    SYS_GETPID = 5,
    SYS_OPEN = 6,
    SYS_CLOSE = 8,
    SYS_BRK = 9,
    SYS_CHDIR = 10,
    SYS_GETCWD = 11,
    SYS_GETDENT = 12,
    SYS_MKDIR = 13,
    SYS_EXECVE = 14,
    SYS_WAITPID = 15,
    SYS_IOCTL = 16,
    SYS_KILL = 17
};

void syscall_init(void);
void syscall_handler(registers_t *regs);

#endif
