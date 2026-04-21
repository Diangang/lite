#ifndef ASM_BARRIER_H
#define ASM_BARRIER_H

/*
 * Linux mapping: x86 exports barrier/smp_* ordering helpers from asm/barrier.h.
 * Lite is still UP-only, but drivers and core headers need the same API surface.
 */

#define barrier() __asm__ volatile("" ::: "memory")

static inline void mb(void)
{
    __sync_synchronize();
}

static inline void rmb(void)
{
    __sync_synchronize();
}

static inline void wmb(void)
{
    __sync_synchronize();
}

#define dma_rmb() rmb()
#define dma_wmb() wmb()

#define smp_mb() mb()
#define smp_rmb() rmb()
#define smp_wmb() wmb()

#define smp_store_mb(var, value)      \
    do {                              \
        (var) = (value);              \
        smp_mb();                     \
    } while (0)

#define smp_store_release(p, v)       \
    do {                              \
        barrier();                    \
        *(p) = (v);                   \
    } while (0)

#define smp_load_acquire(p)           \
    ({                                \
        typeof(*(p)) __v = *(p);      \
        barrier();                    \
        __v;                          \
    })

#define smp_mb__before_atomic() barrier()
#define smp_mb__after_atomic() barrier()

#endif
