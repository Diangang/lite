#include "linux/signal.h"
#include "linux/sched.h"
#include "linux/irqflags.h"
#include "linux/pid.h"
#include "linux/sched.h"
#include "linux/wait.h"

/* signal_wake_task: Implement signal wake task. */
static void signal_wake_task(struct task_struct *t)
{
    if (!t)
        return;
    if (t->state == TASK_SLEEPING) {
        wake_up_process(t);
        return;
    }
    if (t->state == TASK_BLOCKED && t->waitq) {
        wait_queue_remove(t->waitq, t);
        wake_up_process(t);
    }
}

/* signal_notify_exit: Implement signal notify exit. */
void signal_notify_exit(struct task_struct *child)
{
    if (!child || !child->parent)
        return;
    uint32_t flags = irq_save();
    /*
     * Linux mapping: child exit delivers SIGCHLD which can interrupt sleeps,
     * and also wakes waiters in waitpid paths.
     */
    wake_up_process(child->parent);
    wake_up_all(&child->parent->child_exit_wait);
    irq_restore(flags);
}

/* sys_kill: Implement sys kill. */
int sys_kill(uint32_t id, int sig)
{
    struct task_struct *self = task_current();
    if (!(sig == 0 || sig == SIGCHLD || sig == SIGINT || sig == SIGKILL || sig == SIGTERM))
        return -1;
    if (id == 0)
        return -1;
    if (list_empty(&task_list_head))
        return -1;
    uint32_t flags = tasklist_lock();
    struct task_struct *t = find_task_by_vpid(id);
    if (!t || t->pid == 0) {
        tasklist_unlock(flags);
        return -1;
    }
    if (sig == 0) {
        tasklist_unlock(flags);
        return 0;
    }
    if (sig == SIGCHLD) {
        if (!t->parent) {
            tasklist_unlock(flags);
            return -1;
        }
        signal_wake_task(t->parent);
        tasklist_unlock(flags);
        return 0;
    }
    if (t->state == TASK_ZOMBIE) {
        tasklist_unlock(flags);
        return 0;
    }
    if (!t->mm) {
        tasklist_unlock(flags);
        return -1;
    }
    if (t == self) {
        tasklist_unlock(flags);
        do_exit_reason(128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
        return 0;
    }
    if (is_global_init(t)) {
        tasklist_unlock(flags);
        return -1;
    }
    tasklist_unlock(flags);
    exit_notify(t, 128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
    return 0;
}
