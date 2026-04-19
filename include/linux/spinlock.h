#ifndef LINUX_SPINLOCK_H
#define LINUX_SPINLOCK_H

#include <stdint.h>
#include "linux/irqflags.h"

typedef struct spinlock {
    uint32_t irqflags;
} spinlock_t;

#define DEFINE_SPINLOCK(name) spinlock_t name = { 0 }

static inline void spin_lock(spinlock_t *lock)
{
    if (!lock)
        return;
    lock->irqflags = irq_save();
}

static inline void spin_unlock(spinlock_t *lock)
{
    if (!lock)
        return;
    irq_restore(lock->irqflags);
}

#endif
