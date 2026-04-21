#include "linux/sched.h"
#include "linux/sched.h"
#include "linux/pid.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/fs.h"
#include "linux/irqflags.h"
#include "linux/kernel.h"
#include "linux/signal.h"
#include "linux/wait.h"

/* task_free_user_memory: Implement task free user memory. */
static void task_free_user_memory(struct task_struct *task)
{
    if (!task)
        return;
    if (!task->mm)
        return;
    mm_destroy(task->mm);
    task->mm = NULL;
}

/* task_release_resources: Implement task release resources. */
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

/* reparent_children: Implement reparent children. */
static int reparent_children(struct task_struct *parent, struct task_struct *reaper)
{
    if (!reaper || !parent)
        return 0;

    int wake = 0;
    if (list_empty(&parent->children))
        return 0;
    struct task_struct *t;
    struct task_struct *n;
    list_for_each_entry_safe(t, n, &parent->children, sibling) {
        list_del(&t->sibling);
        t->parent = reaper;
        list_add_tail(&t->sibling, &reaper->children);
        if (t->state == TASK_ZOMBIE)
            wake = 1;
    }

    return wake;
}

static struct task_struct *task_reaper_locked(void)
{
    struct task_struct *reaper = find_task_by_vpid(1);
    if (reaper) {
        get_task_struct(reaper);
        return reaper;
    }
    if (!list_empty(&task_list_head)) {
        reaper = list_first_entry(&task_list_head, struct task_struct, tasks);
        get_task_struct(reaper);
        return reaper;
    }
    return NULL;
}

/* task_zombify: Implement task zombify. */
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

/* exit_notify: Implement exit notify. */
void exit_notify(struct task_struct *task, int code, int reason, uint32_t info0, uint32_t info1)
{
    struct task_struct *parent;
    struct task_struct *reaper;

    if (!task)
        return;
    if (task->pid == 0)
        return;
    if (task->state == TASK_ZOMBIE)
        return;

    if (task->state == TASK_BLOCKED && task->waitq)
        wait_queue_remove(task->waitq, task);

    uint32_t flags = tasklist_lock();
    parent = task->parent;
    if (parent)
        get_task_struct(parent);
    reaper = task_reaper_locked();
    if (!reaper)
        reaper = task;
    int wake = reparent_children(task, reaper);
    tasklist_unlock(flags);

    task_zombify(task, code, reason, info0, info1);
    signal_notify_exit(task);
    if (parent)
        wake_up_all(&parent->child_exit_wait);
    if (wake && reaper)
        wake_up_all(&reaper->child_exit_wait);
    if (parent)
        put_task_struct(parent);
    if (reaper && reaper != task)
        put_task_struct(reaper);
    task_set_need_resched();
}

/* release_task: Implement release task. */
void release_task(struct task_struct *task)
{
    if (!task)
        return;
    if (task->pid == 0)
        return;

    uint32_t flags = tasklist_lock();
    if (!list_empty(&task->sibling))
        list_del_init(&task->sibling);
    if (!list_empty(&task->tasks))
        list_del_init(&task->tasks);
    task->parent = NULL;
    tasklist_unlock(flags);

    put_task_struct(task);
}

/* do_exit: Perform exit. */
void do_exit(int code)
{
    struct task_struct *task = task_current();
    if (!task)
        return;
    if (task->pid == 0)
        return;
    do_exit_reason(code, TASK_EXIT_NORMAL, 0, 0);
}

/* do_exit_reason: Perform exit reason. */
void do_exit_reason(int code, int reason, uint32_t info0, uint32_t info1)
{
    struct task_struct *task = task_current();
    if (!task)
        return;
    if (task->pid == 0)
        return;
    exit_notify(task, code, reason, info0, info1);
}

/* sys_exit: Implement sys exit. */
void sys_exit(int code)
{
    do_exit(code);
}
