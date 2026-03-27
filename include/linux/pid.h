#ifndef LINUX_PID_H
#define LINUX_PID_H

#include <stdint.h>

struct task_struct;

struct task_struct *find_task_by_pid(uint32_t pid);

#endif
