#ifndef TASK_INTERNAL_H
#define TASK_INTERNAL_H

#include "task.h"

struct task_struct {
    uint32_t pid;
    struct task_struct *parent;
    struct thread_struct thread;
    uint32_t wake_jiffies;
    int state;
    int time_slice;
    struct mm_struct *mm;
    int exit_code;
    int exit_state;
    uint32_t exit_info0;
    uint32_t exit_info1;
    char comm[32];
    struct fs_struct fs;
    struct files_struct files;
    uint32_t uid;
    uint32_t gid;
    uint32_t umask;
    void *waitq;
    struct task_struct *wait_next;
    struct task_struct *next;
};

enum { TASK_TIMESLICE_TICKS = 3 };

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1,
    TASK_BLOCKED = 2,
    TASK_ZOMBIE = 3
};

extern struct task_struct *task_head;
extern struct task_struct *current;
extern uint32_t next_task_id;
extern wait_queue_t exit_waitq;
extern int need_resched;
extern uint32_t sched_switch_count;

uint32_t irq_save(void);
void irq_restore(uint32_t flags);

void user_task(void);
void task_set_comm(struct task_struct *task, const char *program);
struct pt_regs *copy_thread(uint32_t *stack, void (*entry)(void), struct pt_regs *parent_regs);
struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);
void task_fdtable_init(struct task_struct *task);
void task_fdtable_close_all(struct task_struct *task);
void task_fdtable_clone(struct task_struct *dst, struct task_struct *src);
struct task_struct *task_find_by_pid(uint32_t pid);

#endif
