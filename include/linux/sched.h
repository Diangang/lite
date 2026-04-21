#ifndef LINUX_SCHED_H
#define LINUX_SCHED_H

#include <stdint.h>
#include "linux/atomic.h"
#include "linux/list.h"
#include "linux/wait.h"
#include "linux/mm.h"
#include "linux/fs_struct.h"
#include "linux/fdtable.h"
#include "asm/processor.h"

struct pt_regs;
struct nameidata;

void fork_init(void);
int sys_fork(struct pt_regs *regs);
int kernel_thread(void (*entry)(void));
int kernel_create_user(const char *program);

enum {
    TASK_EXIT_NORMAL = 0,
    TASK_EXIT_EXCEPTION = 1,
    TASK_EXIT_PAGEFAULT = 2,
    TASK_EXIT_SIGNAL = 3
};

void do_exit(int code);
void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1);
void task_zombify(struct task_struct *task, int code, int reason, uint32_t info0, uint32_t info1);
void exit_notify(struct task_struct *task, int code, int reason, uint32_t info0, uint32_t info1);
void release_task(struct task_struct *task);
void sys_exit(int code);

struct task_struct {
    atomic_t usage;
    uint32_t pid;
    struct task_struct *parent;
    struct thread_struct thread;
    struct list_head tasks;
    /* Linux mapping: parent/children/sibling linkage for wait/exit handling. */
    struct list_head children;
    struct list_head sibling;
    /* Linux mapping: waitqueue for child-exit notifications (wait_chldexit). */
    wait_queue_head_t child_exit_wait;
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
    struct nameidata *nameidata;
    struct files_struct files;
    uint32_t uid;
    uint32_t gid;
    uint32_t umask;
    wait_queue_head_t *waitq;
    wait_queue_entry_t wait_entry;
};

enum { TASK_TIMESLICE_TICKS = 3 };

enum {
    TASK_RUNNABLE = 0,
    TASK_SLEEPING = 1,
    TASK_BLOCKED = 2,
    TASK_ZOMBIE = 3
};

/*
 * Linux-shaped lifetime invariant for task release paths:
 * a task_struct must not be freed while still reachable from tasklist,
 * parent/children linkage, or any waitqueue attachment.
 */
static inline int task_release_invariant_holds(struct task_struct *task)
{
    if (!task)
        return 0;
    if (task->pid == 0)
        return 0;
    if (task->state != TASK_ZOMBIE)
        return 0;
    if (task->parent)
        return 0;
    if (!list_empty(&task->tasks))
        return 0;
    if (!list_empty(&task->sibling))
        return 0;
    if (!list_empty(&task->children))
        return 0;
    if (task->waitq)
        return 0;
    return 1;
}

extern struct list_head task_list_head;
/*
 * Linux mapping: Lite still exports `current` and `need_resched` as
 * compatibility mirrors for existing call sites, but scheduler ownership lives
 * in `boot_cpu_sched.*` inside `kernel/sched/core.c`. New core code should prefer
 * helper accessors below instead of consuming these globals directly.
 */
extern struct task_struct *current;
extern uint32_t last_pid;
extern int need_resched;
extern uint32_t sched_switch_count;

uint32_t tasklist_lock(void);
void tasklist_unlock(uint32_t flags);

void set_task_comm(struct task_struct *task, const char *program);

void sched_init(void);
struct pt_regs *task_schedule(struct pt_regs *regs);
void task_tick(void);
void task_sleep(uint32_t ticks);
void task_yield(void);
void task_list(void);
void wake_up_process(struct task_struct *task);
/*
 * Linux mapping: core code should treat these helpers as the supported current
 * task / resched access surface, analogous to Linux current-task and
 * TIF_NEED_RESCHED ownership conventions.
 */
struct task_struct *task_current(void);
void task_set_need_resched(void);
void task_clear_need_resched(void);
int task_need_resched(void);

uint32_t task_get_switch_count(void);
const char *task_get_current_comm(void);
uint32_t task_get_current_id(void);
int task_current_is_user(void);
int task_should_resched(void);

void __put_task_struct(struct task_struct *task);

static inline void get_task_struct(struct task_struct *task)
{
    if (!task)
        return;
    atomic_inc(&task->usage);
}

static inline void put_task_struct(struct task_struct *task)
{
    if (!task)
        return;
    if (atomic_dec_and_test(&task->usage))
        __put_task_struct(task);
}

#endif
