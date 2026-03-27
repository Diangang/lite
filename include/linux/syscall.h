#ifndef LINUX_SYSCALL_H
#define LINUX_SYSCALL_H

#include "asm/ptrace.h"
#include "asm/unistd.h"

void init_syscall(void);
struct pt_regs *syscall_handler(struct pt_regs *regs);

#endif
