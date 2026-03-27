#ifndef ASM_PTRACE_H
#define ASM_PTRACE_H

#include <stdint.h>

struct pt_regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

#endif
