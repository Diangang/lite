#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "isr.h"

enum {
    SYS_WRITE = 0,
    SYS_YIELD = 1,
    SYS_SLEEP = 2
};

void syscall_init(void);
void syscall_handler(registers_t *regs);

#endif
