#ifndef LINUX_SPINLOCK_TYPES_H
#define LINUX_SPINLOCK_TYPES_H

#include <stdint.h>

typedef struct raw_spinlock {
    uint32_t irqflags;
    uint32_t locked;
} raw_spinlock_t;

typedef struct spinlock {
    raw_spinlock_t raw_lock;
} spinlock_t;

#define __RAW_SPIN_LOCK_UNLOCKED(lockname) { 0, 0 }
#define __SPIN_LOCK_UNLOCKED(lockname) { .raw_lock = __RAW_SPIN_LOCK_UNLOCKED(lockname) }

#define DEFINE_SPINLOCK(name) spinlock_t name = __SPIN_LOCK_UNLOCKED(name)
#define DEFINE_RAW_SPINLOCK(name) raw_spinlock_t name = __RAW_SPIN_LOCK_UNLOCKED(name)

#endif
