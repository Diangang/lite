#include "linux/sched.h"
#include "linux/timer.h"
#include "asm/pgtable.h"
#include "asm/gdt.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/fs.h"
#include "linux/irqflags.h"

struct list_head task_list_head = LIST_HEAD_INIT(task_list_head);
struct task_struct *current = NULL;
uint32_t next_task_id = 1;
int need_resched = 0;
uint32_t sched_switch_count = 0;

struct rq {
    struct list_head *tasks;
};

static struct rq system_rq = {0};

static void runqueue_init(struct rq *rq, struct list_head *tasks)
{
    if (!rq)
        return;
    rq->tasks = tasks;
}

static int runqueue_empty(struct rq *rq)
{
    return !rq || !rq->tasks || list_empty(rq->tasks);
}

static void runqueue_wake_sleepers(struct rq *rq, uint32_t now)
{
    if (runqueue_empty(rq))
        return;
    struct task_struct *t;
    list_for_each_entry(t, rq->tasks, tasks) {
        if (t->state == TASK_SLEEPING && (int32_t)(now - t->wake_jiffies) >= 0)
            t->state = TASK_RUNNABLE;
    }
}

static struct task_struct *runqueue_pick_next(struct rq *rq, struct task_struct *curr)
{
    if (runqueue_empty(rq) || !curr)
        return curr;
    struct list_head *head = rq->tasks;
    struct list_head *pos = curr->tasks.next;
    if (pos == head)
        pos = pos->next;
    while (pos != &curr->tasks) {
        struct task_struct *next = list_entry(pos, struct task_struct, tasks);
        if (next->state == TASK_RUNNABLE)
            return next;
        pos = pos->next;
        if (pos == head)
            pos = pos->next;
    }
    return curr;
}

/* task_idle: Implement task idle. */
static void task_idle(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}


/* task_tick: Implement task tick. */
void task_tick(void)
{
    uint32_t now = timer_get_ticks();
    if (runqueue_empty(&system_rq))
        return;
    runqueue_wake_sleepers(&system_rq, now);

    if (!current)
        return;
    if (current->state != TASK_RUNNABLE) {
        need_resched = 1;
        return;
    }

    if (current->time_slice > 0)
        current->time_slice--;
    if (current->time_slice <= 0)
        need_resched = 1;
}

/* task_schedule: Implement task schedule. */
struct pt_regs *task_schedule(struct pt_regs *regs)
{
    if (!current)
        return regs;

    current->thread.regs = regs;
    if (!need_resched)
        return current->thread.regs;

    need_resched = 0;

    struct task_struct *next = runqueue_pick_next(&system_rq, current);
    if (next != current)
        sched_switch_count++;
    current = next;
    if (current->mm && current->mm->pgd) {
        if (current->mm->pgd != get_pgd_current())
            switch_pgd(current->mm->pgd);
    } else {
        pgd_t *kdir = get_pgd_kernel();
        if (kdir && kdir != get_pgd_current())
            switch_pgd(kdir);
    }
    tss_set_kernel_stack((uint32_t)current->thread.sp0);
    if (current->time_slice <= 0)
        current->time_slice = TASK_TIMESLICE_TICKS;

    return current->thread.regs;
}

/* task_sleep: Implement task sleep. */
void task_sleep(uint32_t ticks)
{
    if (!current)
        return;
    if (ticks == 0)
        return;
    current->wake_jiffies = timer_get_ticks() + ticks;
    current->state = TASK_SLEEPING;
    need_resched = 1;
    task_yield();
}

/* task_yield: Implement task yield. */
void task_yield(void)
{
    need_resched = 1;
    __asm__ volatile("int $0x20");
}

/* task_should_resched: Implement task should resched. */
int task_should_resched(void)
{
    if (!current)
        return 0;
    if (current->state != TASK_RUNNABLE)
        return 1;
    return need_resched != 0;
}

/* task_get_switch_count: Implement task get switch count. */
uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}

/* task_get_current_comm: Implement task get current comm. */
const char *task_get_current_comm(void)
{
    if (!current)
        return NULL;
    if (!current->comm[0])
        return NULL;
    return current->comm;
}

/* task_get_current_id: Implement task get current id. */
uint32_t task_get_current_id(void)
{
    if (!current)
        return 0;
    return current->pid;
}

/* task_current_is_user: Implement task current is user. */
int task_current_is_user(void)
{
    if (!current)
        return 0;
    return current->mm != NULL;
}

/* task_list: Implement task list. */
void task_list(void)
{
    if (runqueue_empty(&system_rq))
        return (void)printf("No tasks.\n");

    printf("PID   STATE     WAKE    CURRENT\n");
    struct task_struct *task;
    list_for_each_entry(task, system_rq.tasks, tasks) {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING)
        state = "SLEEPING"; else if (task->state == TASK_BLOCKED)
        state = "BLOCKED"; else if (task->state == TASK_ZOMBIE)
        state = "ZOMBIE";
        printf("%d    %s  %d    %s\n",
               task->pid,
               state,
               task->wake_jiffies,
               task == current ? "yes" : "no");
    }
}

/* init_task: Initialize task. */
static void init_task(void)
{
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack)
        panic("TASK: Failed to initialize tasking.");

    task->pid = 0;
    task->parent = NULL;
    task->thread.regs = copy_thread(stack, task_idle, NULL);
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    INIT_LIST_HEAD(&task->tasks);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = NULL;
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->comm[0] = 0;
    task->fs.pwd = vfs_root_dentry;
    if (task->fs.pwd)
        task->fs.pwd->refcount++;
    task->fs.root = vfs_root_dentry;
    if (task->fs.root)
        task->fs.root->refcount++;
    task->uid = 0;
    task->gid = 0;
    task->umask = 022;
    files_init(task);
    task->waitq = NULL;
    init_waitqueue_entry(&task->wait_entry, task);

    runqueue_init(&system_rq, &task_list_head);
    list_add(&task->tasks, &task_list_head);
    current = task;
    wait_queue_init(&exit_waitq);
    tss_set_kernel_stack((uint32_t)current->thread.sp0);
}

/* sched_init: Initialize sched. */
void sched_init(void)
{
    init_task();
}
