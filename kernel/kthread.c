#include "linux/kthread.h"
#include "linux/fork.h"
#include "linux/pid.h"
#include "linux/sched.h"
#include "linux/slab.h"
#include "linux/irqflags.h"
#include "linux/exit.h"

struct kthread_create_info {
    struct kthread_create_info *next;
    uint32_t pid;
    int (*threadfn)(void *data);
    void *data;
};

static struct kthread_create_info *kthread_create_list;

static struct kthread_create_info *kthread_take_start(uint32_t pid)
{
    struct kthread_create_info *prev = NULL;
    struct kthread_create_info *entry = kthread_create_list;

    while (entry) {
        if (entry->pid == pid) {
            if (prev)
                prev->next = entry->next;
            else
                kthread_create_list = entry->next;
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
    struct kthread_create_info *create;
    int ret = -1;
    uint32_t flags;

    if (!task)
        do_exit(ret);

    flags = irq_save();
    create = kthread_take_start(task->pid);
    irq_restore(flags);
    if (!create)
        do_exit(ret);

    if (create->threadfn)
        ret = create->threadfn(create->data);
    kfree(create);
    do_exit(ret);
}

struct task_struct *kthread_run(int (*threadfn)(void *data), void *data, const char *name)
{
    struct kthread_create_info *create;
    struct task_struct *task;
    uint32_t flags;
    int pid;

    if (!threadfn)
        return NULL;

    create = (struct kthread_create_info *)kmalloc(sizeof(*create));
    if (!create)
        return NULL;

    flags = irq_save();
    pid = kernel_thread(kthread_bootstrap);
    if (pid < 0) {
        irq_restore(flags);
        kfree(create);
        return NULL;
    }

    create->pid = (uint32_t)pid;
    create->threadfn = threadfn;
    create->data = data;
    create->next = kthread_create_list;
    kthread_create_list = create;

    task = find_task_by_vpid((uint32_t)pid);
    if (task && name)
        set_task_comm(task, name);
    irq_restore(flags);
    return task;
}
