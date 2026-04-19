#include "linux/kthread.h"
#include "linux/fork.h"
#include "linux/pid.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/irqflags.h"
#include "linux/exit.h"

struct kthread_start {
    struct kthread_start *next;
    uint32_t pid;
    int (*threadfn)(void *data);
    void *data;
};

static struct kthread_start *kthread_starts;

static struct kthread_start *kthread_take_start(uint32_t pid)
{
    struct kthread_start *prev = NULL;
    struct kthread_start *entry = kthread_starts;

    while (entry) {
        if (entry->pid == pid) {
            if (prev)
                prev->next = entry->next;
            else
                kthread_starts = entry->next;
            entry->next = NULL;
            return entry;
        }
        prev = entry;
        entry = entry->next;
    }
    return NULL;
}

static void kthread_bootstrap(void)
{
    struct task_struct *task = task_current();
    struct kthread_start *start;
    int ret = -1;
    uint32_t flags;

    if (!task)
        do_exit(ret);

    flags = irq_save();
    start = kthread_take_start(task->pid);
    irq_restore(flags);
    if (!start)
        do_exit(ret);

    if (start->threadfn)
        ret = start->threadfn(start->data);
    kfree(start);
    do_exit(ret);
}

struct task_struct *kthread_run(int (*threadfn)(void *data), void *data, const char *name)
{
    struct kthread_start *start;
    struct task_struct *task;
    uint32_t flags;
    int pid;

    if (!threadfn)
        return NULL;

    start = (struct kthread_start *)kmalloc(sizeof(*start));
    if (!start)
        return NULL;

    flags = irq_save();
    pid = kernel_thread(kthread_bootstrap);
    if (pid < 0) {
        irq_restore(flags);
        kfree(start);
        return NULL;
    }

    start->pid = (uint32_t)pid;
    start->threadfn = threadfn;
    start->data = data;
    start->next = kthread_starts;
    kthread_starts = start;

    task = find_task_by_pid((uint32_t)pid);
    if (task && name)
        set_task_comm(task, name);
    irq_restore(flags);
    return task;
}
