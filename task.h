#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "isr.h"

void tasking_init(void);
int task_create(void (*entry)(void));
int task_create_user(const char *program);
registers_t *task_schedule(registers_t *regs);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_list(void);
void task_exit(void);
void task_exit_with_status(int code);
void task_exit_with_reason(int code, int reason, uint32_t info0, uint32_t info1);
int task_wait(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1);
void task_set_current_page_directory(uint32_t* dir);
void task_set_user_info(uint32_t base, uint32_t pages, uint32_t stack_base);
void task_get_user_info(uint32_t *base, uint32_t *pages, uint32_t *stack_base);
const char *task_get_current_program(void);
uint32_t task_get_current_id(void);
void task_set_demo_enabled(int enabled);
int task_get_demo_enabled(void);

enum {
    TASK_EXIT_NORMAL = 0,
    TASK_EXIT_EXCEPTION = 1,
    TASK_EXIT_PAGEFAULT = 2
};

#endif
