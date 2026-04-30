#include "linux/slab.h"
#include "linux/string.h"

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
    void *p;

    (void)gfp;
    p = kmalloc(len);
    if (p)
        memcpy(p, src, len);
    return p;
}
