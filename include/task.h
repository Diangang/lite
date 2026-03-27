#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "isr.h"
#include "file.h"

typedef struct wait_queue {
    void *head;
} wait_queue_t;

struct vm_area_struct {
    uint32_t vm_start;
    uint32_t vm_end;
    uint32_t vm_flags;
    struct vm_area_struct *vm_next;
};

struct mm_struct {
    uint32_t *pgd;
    uint32_t start_code;
    uint32_t end_code;
    uint32_t start_stack;
    uint32_t start_brk;
    uint32_t brk;
    struct vm_area_struct *mmap;
};

struct fd_struct {
    int used;
    struct file *file;
};

enum { TASK_FD_MAX = 32 };
enum { THREAD_SIZE = 4096 };

struct files_struct {
    struct fd_struct fd[TASK_FD_MAX];
};

struct fs_struct {
    struct dentry *pwd;
    struct dentry *root;
};

struct thread_struct {
    struct pt_regs *regs;
    uint32_t *sp0;
};

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
struct dentry *task_get_cwd_dentry(void);
struct dentry *task_get_root_dentry(void);
int task_set_cwd_dentry(struct dentry *d);
int task_execve(const char *program, struct pt_regs *regs);
uint32_t task_mmap(uint32_t addr, uint32_t length, uint32_t prot);
int task_munmap(uint32_t addr, uint32_t length);
int task_fork(struct pt_regs *regs);
uint32_t task_get_uid(void);
uint32_t task_get_gid(void);
uint32_t task_get_umask(void);
uint32_t task_set_umask(uint32_t mask);
void task_user_heap_init(uint32_t heap_base, uint32_t stack_base);
uint32_t task_brk(uint32_t new_end);
int task_fd_alloc(struct file *file);
struct fd_struct *task_fd_get(int fd);
int task_fd_close(int fd);
void task_install_stdio(struct inode *console);

void init_task(void);
void sched_init(void);
void fork_init(void);
int task_create(void (*entry)(void));
int task_create_user(const char *program);
struct pt_regs *task_schedule(struct pt_regs *regs);
void task_tick(void);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_list(void);
void task_exit(void);
void task_exit_with_status(int code);
void task_exit_with_reason(int code, int reason, uint32_t info0, uint32_t info1);
int task_wait(uint32_t id, int *out_code, int *out_reason, uint32_t *out_info0, uint32_t *out_info1);
int task_kill(uint32_t id, int sig);
void task_set_current_page_directory(uint32_t* dir);
void task_set_user_info(uint32_t base, uint32_t pages, uint32_t stack_base);
void task_get_user_info(uint32_t *base, uint32_t *pages, uint32_t *stack_base);
int task_user_vma_allows(uint32_t addr, int is_write, int is_exec);
const char *task_get_current_comm(void);
uint32_t task_get_current_id(void);
int task_current_is_user(void);
int task_should_resched(void);
int task_exec_user(const char *program);

enum {
    TASK_EXIT_NORMAL = 0,
    TASK_EXIT_EXCEPTION = 1,
    TASK_EXIT_PAGEFAULT = 2,
    TASK_EXIT_SIGNAL = 3
};

enum {
    SIGINT = 2
};

#endif
