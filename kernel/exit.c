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
    if (list_empty(&task_list_head) || !reaper || !current)
        return 0;

    int wake = 0;
    struct task_struct *t;
    list_for_each_entry(t, &task_list_head, tasks) {
        if (t->parent == current && t != current) {
            t->parent = reaper;
            if (t->state == TASK_ZOMBIE)
                wake = 1;
        }
    }

    return wake;
}

void task_destroy(struct task_struct *task)
{
    if (!task)
        return;
    if (!current || list_empty(&task_list_head))
        return;
    if (task == current)
        return;
    if (task->pid == 0)
        return;

    uint32_t flags = irq_save();
    list_del(&task->tasks);
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
    if (!reaper) {
        if (!list_empty(&task_list_head))
            reaper = list_first_entry(&task_list_head, struct task_struct, tasks);
        else
            reaper = current;
    }
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
