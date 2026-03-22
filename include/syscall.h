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
    SYS_KILL = 17,
    SYS_MMAP = 18,
    SYS_MUNMAP = 19,
    SYS_FORK = 20,
    SYS_GETDENTS = 21,
    SYS_UMASK = 22,
    SYS_CHMOD = 23,
    SYS_GETUID = 24,
    SYS_GETGID = 25
};

void init_syscall(void);
struct registers *syscall_handler(struct registers *regs);

#endif
