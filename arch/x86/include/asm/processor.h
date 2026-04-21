#ifndef ASM_PROCESSOR_H
#define ASM_PROCESSOR_H

#include <stdint.h>
#include "asm/ptrace.h"

enum { THREAD_SIZE = 4096 };

struct thread_struct {
    struct pt_regs *regs;
    uint32_t *sp0;
};

struct pt_regs *copy_thread(uint32_t *stack, void (*entry)(void), struct pt_regs *parent_regs);

#endif
