#ifndef LINUX_COMPLETION_H
#define LINUX_COMPLETION_H

#include <stdbool.h>
#include <stdint.h>
#include "linux/sched.h"
#include "linux/wait.h"
#include "linux/irqflags.h"

struct completion {
    unsigned int done;
    wait_queue_head_t wait;
};

#define COMPLETION_INITIALIZER(work) \
    { 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait) }

#define COMPLETION_INITIALIZER_ONSTACK(work) \
    ({ init_completion(&work); work; })

#define DECLARE_COMPLETION(work) \
    struct completion work = COMPLETION_INITIALIZER(work)

#ifdef CONFIG_LOCKDEP
#define DECLARE_COMPLETION_ONSTACK(work) \
    struct completion work = COMPLETION_INITIALIZER_ONSTACK(work)
#else
#define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)
#endif

static inline void init_completion(struct completion *x)
{
    if (!x)
        return;
    x->done = 0;
    init_waitqueue_head(&x->wait);
}

static inline void reinit_completion(struct completion *x)
{
    if (!x)
        return;
    x->done = 0;
}

static inline void complete(struct completion *x)
{
    uint32_t flags;

    if (!x)
        return;
    flags = irq_save();
    x->done++;
    wake_up_all(&x->wait);
    irq_restore(flags);
}

static inline void complete_all(struct completion *x)
{
    uint32_t flags;

    if (!x)
        return;
    flags = irq_save();
    x->done += ((uint32_t)~0U) / 2;
    wake_up_all(&x->wait);
    irq_restore(flags);
}

static inline bool try_wait_for_completion(struct completion *x)
{
    uint32_t flags;
    bool ret = true;

    if (!x || !x->done)
        return false;
    flags = irq_save();
    if (!x->done)
        ret = false;
    else
        x->done--;
    irq_restore(flags);
    return ret;
}

static inline bool completion_done(struct completion *x)
{
    if (!x || !x->done)
        return false;
    smp_rmb();
    spin_unlock_wait(&x->wait.lock);
    return true;
}

static inline void wait_for_completion(struct completion *x)
{
    if (!x)
        return;
    for (;;) {
        uint32_t flags = irq_save();
        if (x->done) {
            x->done--;
            irq_restore(flags);
            return;
        }
        irq_restore(flags);
        wait_queue_block(&x->wait);
        task_yield();
    }
}

#endif
