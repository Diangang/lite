#ifndef LINUX_BINFMT_H
#define LINUX_BINFMT_H

#include <stdint.h>

struct pt_regs;

int kernel_load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, uint32_t** out_dir,
                             uint32_t* out_base, uint32_t* out_pages, uint32_t* out_stack_base);
void user_task(void);
int task_exec_user(const char *program);
int sys_execve(const char *program, struct pt_regs *regs);

#endif
