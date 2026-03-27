#ifndef LINUX_FORK_H
#define LINUX_FORK_H

struct pt_regs;

void fork_init(void);
int sys_fork(struct pt_regs *regs);
int kernel_thread(void (*entry)(void));
int kernel_create_user(const char *program);

#endif
