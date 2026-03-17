#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "isr.h"

void tasking_init(void);
int task_create(void (*entry)(void));
registers_t *task_schedule(registers_t *regs);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_list(void);
void task_exit(void);
void task_set_current_page_directory(uint32_t* dir);
void task_set_user_info(uint32_t base, uint32_t pages, uint32_t stack_base);
void task_set_demo_enabled(int enabled);
int task_get_demo_enabled(void);

#endif
