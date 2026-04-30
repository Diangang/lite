#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#include "asm/atomic.h"
#include "asm/barrier.h"

/*
 * Linux mapping: generic atomic wrappers layer acquire/release helpers on top
 * of asm/atomic.h. Lite keeps the same surface for later SMP-safe convergence.
 */

#ifndef atomic_read_acquire
#define atomic_read_acquire(v) smp_load_acquire(&(v)->counter)
#endif

#ifndef atomic_set_release
#define atomic_set_release(v, i) smp_store_release(&(v)->counter, (i))
#endif

#ifndef atomic_add_return_relaxed
#define atomic_add_return_relaxed atomic_add_return
#define atomic_add_return_acquire atomic_add_return
#define atomic_add_return_release atomic_add_return
#endif

#ifndef atomic_inc_return_relaxed
#define atomic_inc_return_relaxed atomic_inc_return
#define atomic_inc_return_acquire atomic_inc_return
#define atomic_inc_return_release atomic_inc_return
#endif

#ifndef atomic_sub_return_relaxed
#define atomic_sub_return_relaxed atomic_sub_return
#define atomic_sub_return_acquire atomic_sub_return
#define atomic_sub_return_release atomic_sub_return
#endif

#ifndef atomic_dec_return_relaxed
#define atomic_dec_return_relaxed atomic_dec_return
#define atomic_dec_return_acquire atomic_dec_return
#define atomic_dec_return_release atomic_dec_return
#endif

#ifndef atomic_xchg_relaxed
#define atomic_xchg_relaxed atomic_xchg
#define atomic_xchg_acquire atomic_xchg
#define atomic_xchg_release atomic_xchg
#endif

#ifndef atomic_cmpxchg_relaxed
#define atomic_cmpxchg_relaxed atomic_cmpxchg
#define atomic_cmpxchg_acquire atomic_cmpxchg
#define atomic_cmpxchg_release atomic_cmpxchg
#endif

#endif
