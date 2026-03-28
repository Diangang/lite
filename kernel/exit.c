#include "linux/sched.h"
#include "linux/exit.h"
#include "linux/pid.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/irqflags.h"
#include "linux/wait.h"

static void task_free_user_memory(struct task_struct *task)
{
    if (!task)
        return;
    if (!task->mm)
        return;
    mm_destroy(task->mm);
    task->mm = NULL;
}

static int reparent_children(struct task_struct *reaper)
{
    if (!task_head || !reaper || !current)
        return 0;

    int wake = 0;
    struct task_struct *t = task_head;
    do {
        if (t->parent == current && t != current) {
            t->parent = reaper;
            if (t->state == TASK_ZOMBIE)
                wake = 1;
        }
        t = t->next;
    } while (t && t != task_head);

    return wake;
}

void task_destroy(struct task_struct *prev, struct task_struct *task)
{
    if (!prev || !task)
        return;
    if (!task_head || !current)
        return;
    if (task == current)
        return;

    uint32_t flags = irq_save();
    struct task_struct *next = task->next;
    if (task == task_head)
        task_head = next;
    prev->next = next;
    irq_restore(flags);

    files_close_all(task);
    task_free_user_memory(task);
    if (task->thread.sp0)
        kfree((void*)((uint32_t)task->thread.sp0 - THREAD_SIZE));
    kfree(task);
}

void do_exit(int code)
{
    if (!current)
        return;
    if (current->pid == 0)
        return;
    do_exit_reason(code, TASK_EXIT_NORMAL, 0, 0);
}

void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1)
{
    if (!current)
        return;
    if (current->pid == 0)
        return;
    if (current->state == TASK_ZOMBIE)
        return;
    if (current->state == TASK_BLOCKED && current->waitq)
        wait_queue_remove(current->waitq, current);

    struct task_struct *reaper = find_task_by_pid(1);
    if (!reaper)
        reaper = task_head;
    uint32_t flags = irq_save();
    int wake = reparent_children(reaper);
    irq_restore(flags);
    if (wake)
        wait_queue_wake_all(&exit_waitq);

    current->exit_code = code;
    current->exit_state = reason;
    current->exit_info0 = info0;
    current->exit_info1 = info1;
    current->state = TASK_ZOMBIE;
    wait_queue_wake_all(&exit_waitq);
    need_resched = 1;
    return;
}

void sys_exit(int code)
{
    do_exit(code);
}
