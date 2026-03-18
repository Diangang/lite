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
    SYS_BRK = 9
};

void syscall_init(void);
void syscall_handler(registers_t *regs);

#endif
