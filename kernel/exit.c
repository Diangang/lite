#include "linux/sched.h"
#include "linux/exit.h"
#include "linux/pid.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/fs.h"
#include "linux/irqflags.h"
#include "linux/signal.h"
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

static void task_release_resources(struct task_struct *task)
{
    if (!task)
        return;
    files_close_all(task);
    task_free_user_memory(task);
    if (task->fs.pwd) {
        vfs_dentry_put(task->fs.pwd);
        task->fs.pwd = NULL;
    }
    if (task->fs.root) {
        vfs_dentry_put(task->fs.root);
        task->fs.root = NULL;
    }
}

static int reparent_children(struct task_struct *parent, struct task_struct *reaper)
{
    if (list_empty(&task_list_head) || !reaper || !parent)
        return 0;

    int wake = 0;
    struct task_struct *t;
    list_for_each_entry(t, &task_list_head, tasks) {
        if (t->parent == parent && t != parent) {
            t->parent = reaper;
            if (t->state == TASK_ZOMBIE)
                wake = 1;
        }
    }

    return wake;
}

void task_zombify(struct task_struct *task, int code, int reason, uint32_t info0, uint32_t info1)
{
    if (!task)
        return;
    if (task->pid == 0)
        return;
    if (task->state == TASK_ZOMBIE)
        return;

    task_release_resources(task);

    task->exit_code = code;
    task->exit_state = reason;
    task->exit_info0 = info0;
    task->exit_info1 = info1;
    task->state = TASK_ZOMBIE;
}

void exit_notify(struct task_struct *task, int code, int reason, uint32_t info0, uint32_t info1)
{
    if (!task)
        return;
    if (task->pid == 0)
        return;
    if (task->state == TASK_ZOMBIE)
        return;

    if (task->state == TASK_BLOCKED && task->waitq)
        wait_queue_remove(task->waitq, task);

    struct task_struct *reaper = find_task_by_pid(1);
    if (!reaper) {
        if (!list_empty(&task_list_head))
            reaper = list_first_entry(&task_list_head, struct task_struct, tasks);
        else
            reaper = task;
    }

    uint32_t flags = irq_save();
    int wake = reparent_children(task, reaper);
    irq_restore(flags);

    task_zombify(task, code, reason, info0, info1);
    signal_notify_exit(task);
    wait_queue_wake_all(&exit_waitq);
    if (wake)
        wait_queue_wake_all(&exit_waitq);
    need_resched = 1;
}

void release_task(struct task_struct *task)
{
    task_destroy(task);
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
    exit_notify(current, code, reason, info0, info1);
}

void sys_exit(int code)
{
    do_exit(code);
}
