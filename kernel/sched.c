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
wait_queue_t exit_waitq = {0};
int need_resched = 0;
uint32_t sched_switch_count = 0;

void wait_queue_init(wait_queue_t *q)
{
    if (!q)
        return;
    q->head = NULL;
}

static void task_idle(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}

void wait_queue_block(wait_queue_t *q)
{
    if (!q)
        return;
    if (!current)
        return;
    uint32_t flags = irq_save();
    wait_queue_block_locked(q);
    irq_restore(flags);
}

void wait_queue_block_locked(wait_queue_t *q)
{
    if (!q)
        return;
    if (!current)
        return;
    if (current->waitq == q)
        return;
    if (current->state == TASK_ZOMBIE)
        return;

    current->waitq = q;
    current->wait_next = (struct task_struct*)q->head;
    q->head = current;
    current->state = TASK_BLOCKED;
}

void wait_queue_wake_all(wait_queue_t *q)
{
    if (!q)
        return;

    uint32_t flags = irq_save();
    struct task_struct *t = (struct task_struct*)q->head;
    q->head = NULL;
    int woke = 0;
    while (t) {
        struct task_struct *next = t->wait_next;
        t->wait_next = NULL;
        t->waitq = NULL;
        if (t->state == TASK_BLOCKED) {
            t->state = TASK_RUNNABLE;
            if (t->time_slice <= 0)
                t->time_slice = TASK_TIMESLICE_TICKS;
            woke = 1;
        }
        t = next;
    }
    if (woke)
        need_resched = 1;
    irq_restore(flags);
}

void wait_queue_remove(wait_queue_t *q, struct task_struct *task)
{
    if (!q)
        return;
    if (!task)
        return;

    uint32_t flags = irq_save();
    struct task_struct *prev = NULL;
    struct task_struct *t = (struct task_struct*)q->head;
    while (t) {
        if (t == task) {
            if (prev)
                prev->wait_next = t->wait_next;
            else
                q->head = t->wait_next;
            break;
        }
        prev = t;
        t = t->wait_next;
    }
    task->wait_next = NULL;
    task->waitq = NULL;
    irq_restore(flags);
}

void task_tick(void)
{
    uint32_t now = timer_get_ticks();
    if (list_empty(&task_list_head))
        return;

    struct task_struct *t;
    list_for_each_entry(t, &task_list_head, tasks) {
        if (t->state == TASK_SLEEPING) {
            if ((int32_t)(now - t->wake_jiffies) >= 0)
                t->state = TASK_RUNNABLE;
        }
    }

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

struct pt_regs *task_schedule(struct pt_regs *regs)
{
    if (!current)
        return regs;

    current->thread.regs = regs;
    if (!need_resched)
        return current->thread.regs;

    need_resched = 0;

    struct task_struct *next = current;
    struct list_head *pos = current->tasks.next;
    if (pos == &task_list_head)
        pos = pos->next;
    while (pos != &current->tasks) {
        next = list_entry(pos, struct task_struct, tasks);
        if (next->state == TASK_RUNNABLE) {
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

        pos = pos->next;
        if (pos == &task_list_head)
            pos = pos->next;
    }

    return current->thread.regs;
}

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

void task_yield(void)
{
    need_resched = 1;
    __asm__ volatile("int $0x20");
}

int task_should_resched(void)
{
    if (!current)
        return 0;
    if (current->state != TASK_RUNNABLE)
        return 1;
    return need_resched != 0;
}

uint32_t task_get_switch_count(void)
{
    return sched_switch_count;
}

const char *task_get_current_comm(void)
{
    if (!current)
        return NULL;
    if (!current->comm[0])
        return NULL;
    return current->comm;
}

uint32_t task_get_current_id(void)
{
    if (!current)
        return 0;
    return current->pid;
}

int task_current_is_user(void)
{
    if (!current)
        return 0;
    return current->mm != NULL;
}

void task_list(void)
{
    if (list_empty(&task_list_head))
        return (void)printf("No tasks.\n");

    printf("PID   STATE     WAKE    CURRENT\n");
    struct task_struct *task;
    list_for_each_entry(task, &task_list_head, tasks) {
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
    task->wait_next = NULL;

    list_add(&task->tasks, &task_list_head);
    current = task;
    wait_queue_init(&exit_waitq);
    tss_set_kernel_stack((uint32_t)current->thread.sp0);
}

void sched_init(void)
{
    init_task();
}
