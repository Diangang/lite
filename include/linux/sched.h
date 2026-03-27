#ifndef LINUX_SCHED_H
#define LINUX_SCHED_H

#include <stdint.h>
#include "linux/wait.h"
#include "linux/mm.h"
#include "linux/fs_struct.h"
#include "linux/fdtable.h"
#include "asm/processor.h"

struct pt_regs;

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

void set_task_comm(struct task_struct *task, const char *program);

void init_task(void);
void sched_init(void);
struct pt_regs *task_schedule(struct pt_regs *regs);
void task_tick(void);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_list(void);

uint32_t task_get_switch_count(void);
const char *task_get_current_comm(void);
uint32_t task_get_current_id(void);
int task_current_is_user(void);
int task_should_resched(void);

#endif
