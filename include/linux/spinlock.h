#ifndef LINUX_SPINLOCK_H
#define LINUX_SPINLOCK_H

#include <stdint.h>
#include "linux/irqflags.h"
#include "asm/barrier.h"
#include "linux/spinlock_types.h"

#ifndef smp_mb__before_spinlock
#define smp_mb__before_spinlock() smp_wmb()
#endif

static inline void raw_spin_lock_init(raw_spinlock_t *lock)
{
    if (!lock)
        return;
    lock->irqflags = 0;
    lock->locked = 0;
}

static inline void spin_lock_init(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_lock_init(&lock->raw_lock);
}

static inline void raw_spin_lock(raw_spinlock_t *lock)
{
    if (!lock)
        return;
    lock->irqflags = irq_save();
    lock->locked = 1;
    barrier();
}

static inline void raw_spin_unlock(raw_spinlock_t *lock)
{
    if (!lock)
        return;
    barrier();
    lock->locked = 0;
    irq_restore(lock->irqflags);
}

static inline int raw_spin_is_locked(raw_spinlock_t *lock)
{
    return lock ? (int)lock->locked : 0;
}

#define raw_spin_is_contended(lock) (((void)(lock), 0))

static inline void raw_spin_unlock_wait(raw_spinlock_t *lock)
{
    while (raw_spin_is_locked(lock))
        barrier();
}

static inline int raw_spin_trylock(raw_spinlock_t *lock)
{
    raw_spin_lock(lock);
    return 1;
}

static inline void raw_spin_lock_irq(raw_spinlock_t *lock)
{
    raw_spin_lock(lock);
}

static inline void raw_spin_unlock_irq(raw_spinlock_t *lock)
{
    raw_spin_unlock(lock);
}

static inline void raw_spin_lock_bh(raw_spinlock_t *lock)
{
    raw_spin_lock(lock);
}

static inline void raw_spin_unlock_bh(raw_spinlock_t *lock)
{
    raw_spin_unlock(lock);
}

static inline void raw_spin_lock_irqsave_impl(raw_spinlock_t *lock, unsigned long *flags)
{
    if (!flags)
        return;
    *flags = irq_save();
    if (!lock)
        return;
    lock->irqflags = (uint32_t)(*flags);
    lock->locked = 1;
    barrier();
}

static inline void raw_spin_unlock_irqrestore_impl(raw_spinlock_t *lock, unsigned long flags)
{
    if (lock) {
        barrier();
        lock->locked = 0;
    }
    irq_restore((uint32_t)flags);
}

#define raw_spin_lock_irqsave(lock, flags) \
    do { raw_spin_lock_irqsave_impl((lock), &(flags)); } while (0)

#define raw_spin_unlock_irqrestore(lock, flags) \
    do { raw_spin_unlock_irqrestore_impl((lock), (flags)); } while (0)

static inline void spin_lock(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_lock(&lock->raw_lock);
}

static inline void spin_unlock(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_unlock(&lock->raw_lock);
}

static inline int spin_trylock(spinlock_t *lock)
{
    return lock ? raw_spin_trylock(&lock->raw_lock) : 0;
}

static inline int spin_is_locked(spinlock_t *lock)
{
    return lock ? raw_spin_is_locked(&lock->raw_lock) : 0;
}

static inline void spin_unlock_wait(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_unlock_wait(&lock->raw_lock);
}

static inline void spin_lock_irq(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_lock_irq(&lock->raw_lock);
}

static inline void spin_unlock_irq(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_unlock_irq(&lock->raw_lock);
}

static inline void spin_lock_bh(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_lock_bh(&lock->raw_lock);
}

static inline void spin_unlock_bh(spinlock_t *lock)
{
    if (!lock)
        return;
    raw_spin_unlock_bh(&lock->raw_lock);
}

#define spin_lock_irqsave(lock, flags) \
    do { raw_spin_lock_irqsave(&(lock)->raw_lock, flags); } while (0)

#define spin_unlock_irqrestore(lock, flags) \
    do { raw_spin_unlock_irqrestore(&(lock)->raw_lock, flags); } while (0)

#endif
