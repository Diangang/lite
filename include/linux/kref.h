#ifndef LINUX_KREF_H
#define LINUX_KREF_H

#include <stdint.h>

struct kref {
    uint32_t refcount;
};

static inline void kref_init(struct kref *kref)
{
    if (!kref)
        return;
    kref->refcount = 1;
}

static inline void kref_get(struct kref *kref)
{
    if (!kref)
        return;
    if (kref->refcount < 0xFFFFFFFFu)
        kref->refcount++;
}

static inline int kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
    if (!kref)
        return 0;
    if (kref->refcount == 0)
        return 0;
    kref->refcount--;
    if (kref->refcount > 0)
        return 0;
    if (release)
        release(kref);
    return 1;
}

#endif
