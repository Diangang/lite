#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "isr.h"
#include "file.h"

typedef struct wait_queue {
    void *head;
} wait_queue_t;

typedef struct vma {
    uint32_t start;
    uint32_t end;
    uint32_t flags;
    struct vma *next;
} vma_t;

typedef struct mm {
    uint32_t *page_directory;
    uint32_t user_base;
    uint32_t user_pages;
    uint32_t user_stack_base;
    uint32_t heap_base;
    uint32_t heap_brk;
    vma_t *vma_list;
} mm_t;

typedef struct task_fd {
    int used;
    struct file *file;
} task_fd_t;

enum { TASK_FD_MAX = 32 };

enum {
    VMA_READ = 1 << 0,
    VMA_WRITE = 1 << 1,
    VMA_EXEC = 1 << 2
};

void wait_queue_init(wait_queue_t *q);
void wait_queue_block(wait_queue_t *q);
void wait_queue_block_locked(wait_queue_t *q);
void wait_queue_wake_all(wait_queue_t *q);

void task_user_vmas_reset(void);
void task_user_vma_add(uint32_t start, uint32_t end, uint32_t flags);

uint32_t task_get_switch_count(void);
uint32_t task_dump_tasks(char *buf, uint32_t len);
uint32_t task_dump_maps(char *buf, uint32_t len);
uint32_t task_dump_maps_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_stat_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_cmdline_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_status_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_fd_pid(uint32_t pid, uint32_t fd, char *buf, uint32_t len);
uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len);
const char *task_get_cwd(void);
int task_set_cwd(const char *cwd);
int task_execve(const char *program, registers_t *regs);
void task_user_heap_init(uint32_t heap_base, uint32_t stack_base);
uint32_t task_brk(uint32_t new_end);
int task_fd_alloc(file_t *file);
task_fd_t *task_fd_get(int fd);
int task_fd_close(int fd);
void task_install_stdio(fs_node_t *console);

void tasking_init(void);
int task_create(void (*entry)(void));
int task_create_user(const char *program);
registers_t *task_schedule(registers_t *regs);
void task_tick(void);
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
int task_user_vma_allows(uint32_t addr, int is_write, int is_exec);
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
