#ifndef _LINUX_UACCESS_H
#define _LINUX_UACCESS_H

#include <stdint.h>
#include "linux/vmm.h"

static inline int copy_from_user(void *to, const void *from, uint32_t n)
{
    return vmm_copyin(to, (void*)from, n);
}

static inline int copy_to_user(void *to, const void *from, uint32_t n)
{
    return vmm_copyout(to, from, n);
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
        if (vmm_copyin(&c, (void*)((uint32_t)src + i), 1) != 0)
            return -1;
        dst[i] = c;
        if (c == 0)
            return 0;
    }
    dst[i] = 0;
    return 0;
}

#endif
