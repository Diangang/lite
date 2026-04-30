#include "linux/sched.h"
#include "linux/time.h"
#include "asm/pgtable.h"
#include "asm/desc.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/fs.h"
#include "linux/irqflags.h"

struct list_head task_list_head = LIST_HEAD_INIT(task_list_head);
/*
 * Linux mapping: these globals emulate the historical direct current-task /
 * need-resched surface for existing Lite call sites. Ownership lives in the
 * boot CPU scheduler state below; new core code should use helpers instead.
 */
struct task_struct *current = NULL;
uint32_t last_pid = 0;
int need_resched = 0;
uint32_t sched_switch_count = 0;

uint32_t tasklist_lock(void)
{
    return irq_save();
}

void tasklist_unlock(uint32_t flags)
{
    irq_restore(flags);
}

struct rq {
    struct list_head *tasks;
};

struct sched_cpu_state {
    struct rq rq;
    struct task_struct *current;
    int need_resched;
};

static struct sched_cpu_state boot_cpu_sched = {0};

static void sched_sync_compat_globals(void)
{
    /* Keep exported mirrors readable for legacy callers while owner state stays per-CPU-shaped. */
    current = boot_cpu_sched.current;
    need_resched = boot_cpu_sched.need_resched;
}

static void sched_set_current(struct task_struct *task)
{
    boot_cpu_sched.current = task;
    sched_sync_compat_globals();
}

static struct sched_cpu_state *sched_boot_cpu(void)
{
    return &boot_cpu_sched;
}

struct task_struct *task_current(void)
{
    return sched_boot_cpu()->current;
}

void task_set_need_resched(void)
{
    sched_boot_cpu()->need_resched = 1;
    sched_sync_compat_globals();
}

void task_clear_need_resched(void)
{
    sched_boot_cpu()->need_resched = 0;
    sched_sync_compat_globals();
}

int task_need_resched(void)
{
    return sched_boot_cpu()->need_resched;
}

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
    struct sched_cpu_state *cpu = sched_boot_cpu();
    uint32_t now = time_get_jiffies();
    if (runqueue_empty(&cpu->rq))
        return;
    runqueue_wake_sleepers(&cpu->rq, now);

    if (!cpu->current)
        return;
    if (cpu->current->state != TASK_RUNNABLE) {
        task_set_need_resched();
        return;
    }

    if (cpu->current->time_slice > 0)
        cpu->current->time_slice--;
    if (cpu->current->time_slice <= 0)
        task_set_need_resched();
}

/* task_schedule: Implement task schedule. */
struct pt_regs *task_schedule(struct pt_regs *regs)
{
    struct sched_cpu_state *cpu = sched_boot_cpu();

    if (!cpu->current)
        return regs;

    cpu->current->thread.regs = regs;
    if (!task_need_resched())
        return cpu->current->thread.regs;

    task_clear_need_resched();

    struct task_struct *next = runqueue_pick_next(&cpu->rq, cpu->current);
    if (next != cpu->current)
        sched_switch_count++;
    sched_set_current(next);
    if (cpu->current->mm && cpu->current->mm->pgd) {
        if (cpu->current->mm->pgd != get_pgd_current())
            switch_pgd(cpu->current->mm->pgd);
    } else {
        pgd_t *kdir = get_pgd_kernel();
        if (kdir && kdir != get_pgd_current())
            switch_pgd(kdir);
    }
    tss_set_kernel_stack((uint32_t)cpu->current->thread.sp0);
    if (cpu->current->time_slice <= 0)
        cpu->current->time_slice = TASK_TIMESLICE_TICKS;

    return cpu->current->thread.regs;
}

/* task_sleep: Implement task sleep. */
void task_sleep(uint32_t ticks)
{
    struct task_struct *task = task_current();
    if (!task)
        return;
    if (ticks == 0)
        return;
    task->wake_jiffies = time_get_jiffies() + ticks;
    task->state = TASK_SLEEPING;
    task_set_need_resched();
    task_yield();
}

/* task_yield: Implement task yield. */
void task_yield(void)
{
    task_set_need_resched();
    __asm__ volatile("int $0x20");
}

/* wake_up_process: Linux-like wakeup helper for sleeping/blocked tasks. */
void wake_up_process(struct task_struct *task)
{
    if (!task)
        return;
    if (task->state == TASK_SLEEPING) {
        task->wake_jiffies = 0;
        task->state = TASK_RUNNABLE;
    } else if (task->state == TASK_BLOCKED) {
        task->state = TASK_RUNNABLE;
    } else {
        return;
    }
    if (task->time_slice <= 0)
        task->time_slice = TASK_TIMESLICE_TICKS;
    task_set_need_resched();
}

/* task_should_resched: Implement task should resched. */
int task_should_resched(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    if (task->state != TASK_RUNNABLE)
        return 1;
    return task_need_resched() != 0;
}

/* task_get_switch_count: Implement task get switch count. */
uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}

/* task_get_current_comm: Implement task get current comm. */
const char *task_get_current_comm(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return NULL;
    if (!task->comm[0])
        return NULL;
    return task->comm;
}

/* task_get_current_id: Implement task get current id. */
uint32_t task_get_current_id(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->pid;
}

/* task_current_is_user: Implement task current is user. */
int task_current_is_user(void)
{
    struct task_struct *task = task_current();
    if (!task)
        return 0;
    return task->mm != NULL;
}

/* task_list: Implement task list. */
void task_list(void)
{
    struct sched_cpu_state *cpu = sched_boot_cpu();
    if (runqueue_empty(&cpu->rq))
        return (void)printf("No tasks.\n");

    printf("PID   STATE     WAKE    CURRENT\n");
    struct task_struct *task;
    list_for_each_entry(task, cpu->rq.tasks, tasks) {
        const char *state = "RUNNABLE";
        if (task->state == TASK_SLEEPING)
        state = "SLEEPING"; else if (task->state == TASK_BLOCKED)
        state = "BLOCKED"; else if (task->state == TASK_ZOMBIE)
        state = "ZOMBIE";
        printf("%d    %s  %d    %s\n",
               task->pid,
               state,
               task->wake_jiffies,
               task == task_current() ? "yes" : "no");
    }
}

/* init_task: Initialize task. */
static void init_task(void)
{
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack)
        panic("TASK: Failed to initialize tasking.");

    atomic_set(&task->usage, 1);
    task->pid = 0;
    task->real_parent = NULL;
    task->parent = NULL;
    task->thread.regs = copy_thread(stack, task_idle, NULL);
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    INIT_LIST_HEAD(&task->tasks);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sibling);
    init_waitqueue_head(&task->child_exit_wait);
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
    task->nameidata = NULL;
    files_init(task);
    task->waitq = NULL;
    init_waitqueue_entry(&task->wait_entry, task);

    runqueue_init(&boot_cpu_sched.rq, &task_list_head);
    list_add(&task->tasks, &task_list_head);
    sched_set_current(task);
    task_clear_need_resched();
    tss_set_kernel_stack((uint32_t)boot_cpu_sched.current->thread.sp0);
}

/* sched_init: Initialize sched. */
void sched_init(void)
{
    init_task();
}

