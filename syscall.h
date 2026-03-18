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
    SYS_GETPID = 5
};

void syscall_init(void);
void syscall_handler(registers_t *regs);

#endif
