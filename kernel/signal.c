#include "linux/signal.h"
#include "linux/exit.h"
#include "linux/irqflags.h"
#include "linux/pid.h"
#include "linux/sched.h"
#include "linux/wait.h"

int sys_kill(uint32_t id, int sig)
{
    if (sig < 0 || sig > SIGTERM)
        return -1;
    if (id == 0)
        return -1;
    if (!task_head)
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
    if (t->state == TASK_BLOCKED && t->waitq)
        wait_queue_remove(t->waitq, t);
    t->exit_code = 128 + sig;
    t->exit_state = TASK_EXIT_SIGNAL;
    t->exit_info0 = (uint32_t)sig;
    t->exit_info1 = 0;
    t->state = TASK_ZOMBIE;
    wait_queue_wake_all(&exit_waitq);
    irq_restore(flags);
    return 0;
}
