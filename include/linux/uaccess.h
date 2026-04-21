#ifndef _LINUX_UACCESS_H
#define _LINUX_UACCESS_H

#include <stdint.h>
#include "asm/pgtable.h"
#include "asm/barrier.h"

/*
 * Linux mapping: pagefault_disable()/enable() control whether user accesses
 * may fault and sleep. Lite currently keeps a UP-only no-op subset, but the
 * interface is exposed so later subsystems can converge on the same contract.
 */

static inline void pagefault_disable(void)
{
    barrier();
}

static inline void pagefault_enable(void)
{
    barrier();
}

static inline int pagefault_disabled(void)
{
    return 0;
}

static inline int faulthandler_disabled(void)
{
    return pagefault_disabled();
}

static inline int copy_from_user(void *to, const void *from, uint32_t n)
{
    return __copy_from_user(to, (void*)from, n);
}

static inline int copy_to_user(void *to, const void *from, uint32_t n)
{
    return __copy_to_user((void*)to, from, n);
}

static inline int strncpy_from_user(char *dst, uint32_t dst_len, const char *src)
{
    if (!dst || dst_len == 0)
        return -1;
    if (!src)
        return -1;

    uint32_t i = 0;
    for (; i + 1 < dst_len; i++) {
        char c = 0;
        if (__copy_from_user(&c, (void*)((uint32_t)src + i), 1) != 0)
            return -1;
        dst[i] = c;
        if (c == 0)
            return 0;
    }
    dst[i] = 0;
    return 0;
}

#endif
