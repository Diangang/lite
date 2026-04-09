#include "linux/signal.h"
#include "linux/exit.h"
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
        t->wake_jiffies = 0;
        t->state = TASK_RUNNABLE;
        if (t->time_slice <= 0)
            t->time_slice = TASK_TIMESLICE_TICKS;
        need_resched = 1;
        return;
    }
    if (t->state == TASK_BLOCKED && t->waitq) {
        wait_queue_remove(t->waitq, t);
        if (t->time_slice <= 0)
            t->time_slice = TASK_TIMESLICE_TICKS;
        t->state = TASK_RUNNABLE;
        need_resched = 1;
    }
}

/* signal_notify_exit: Implement signal notify exit. */
void signal_notify_exit(struct task_struct *child)
{
    if (!child || !child->parent)
        return;
    uint32_t flags = irq_save();
    signal_wake_task(child->parent);
    irq_restore(flags);
}

/* sys_kill: Implement sys kill. */
int sys_kill(uint32_t id, int sig)
{
    if (!(sig == 0 || sig == SIGCHLD || sig == SIGINT || sig == SIGKILL || sig == SIGTERM))
        return -1;
    if (id == 0)
        return -1;
    if (list_empty(&task_list_head))
        return -1;
    uint32_t flags = irq_save();
    struct task_struct *t = find_task_by_pid(id);
    if (!t || t->pid == 0) {
        irq_restore(flags);
        return -1;
    }
    if (sig == 0) {
        irq_restore(flags);
        return 0;
    }
    if (sig == SIGCHLD) {
        if (!t->parent) {
            irq_restore(flags);
            return -1;
        }
        signal_wake_task(t->parent);
        irq_restore(flags);
        return 0;
    }
    if (t->state == TASK_ZOMBIE) {
        irq_restore(flags);
        return 0;
    }
    if (!t->mm) {
        irq_restore(flags);
        return -1;
    }
    if (t == current) {
        irq_restore(flags);
        do_exit_reason(128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
        return 0;
    }
    if (t->pid == 1) {
        irq_restore(flags);
        return -1;
    }
    irq_restore(flags);
    exit_notify(t, 128 + sig, TASK_EXIT_SIGNAL, (uint32_t)sig, 0);
    return 0;
}
