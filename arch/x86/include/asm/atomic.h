#ifndef ASM_ATOMIC_H
#define ASM_ATOMIC_H

#include <stdint.h>

/*
 * Linux mapping: x86 atomic helpers live in asm/atomic.h.
 * Lite currently relies on GCC atomic builtins to provide the same API shape.
 */

typedef struct {
    int counter;
} atomic_t;

#define ATOMIC_INIT(i) { (i) }

static inline int atomic_read(const atomic_t *v)
{
    return v ? v->counter : 0;
}

static inline void atomic_set(atomic_t *v, int i)
{
    if (!v)
        return;
    v->counter = i;
}

static inline void atomic_add(int i, atomic_t *v)
{
    if (!v)
        return;
    __sync_fetch_and_add(&v->counter, i);
}

static inline void atomic_sub(int i, atomic_t *v)
{
    if (!v)
        return;
    __sync_fetch_and_sub(&v->counter, i);
}

static inline void atomic_inc(atomic_t *v)
{
    atomic_add(1, v);
}

static inline void atomic_dec(atomic_t *v)
{
    atomic_sub(1, v);
}

static inline int atomic_sub_and_test(int i, atomic_t *v)
{
    if (!v)
        return 0;
    return __sync_sub_and_fetch(&v->counter, i) == 0;
}

static inline int atomic_dec_and_test(atomic_t *v)
{
    return atomic_sub_and_test(1, v);
}

static inline int atomic_inc_and_test(atomic_t *v)
{
    if (!v)
        return 0;
    return __sync_add_and_fetch(&v->counter, 1) == 0;
}

static inline int atomic_add_negative(int i, atomic_t *v)
{
    if (!v)
        return 0;
    return __sync_add_and_fetch(&v->counter, i) < 0;
}

static inline int atomic_add_return(int i, atomic_t *v)
{
    if (!v)
        return 0;
    return __sync_add_and_fetch(&v->counter, i);
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
    if (!v)
        return 0;
    return __sync_sub_and_fetch(&v->counter, i);
}

#define atomic_inc_return(v) atomic_add_return(1, (v))
#define atomic_dec_return(v) atomic_sub_return(1, (v))

static inline int atomic_cmpxchg(atomic_t *v, int old, int newv)
{
    if (!v)
        return old;
    return __sync_val_compare_and_swap(&v->counter, old, newv);
}

static inline int atomic_xchg(atomic_t *v, int newv)
{
    if (!v)
        return newv;
    return __sync_lock_test_and_set(&v->counter, newv);
}

#endif
