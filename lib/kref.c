#include "linux/kref.h"

/* kref_init: Initialize kref. */
void kref_init(struct kref *kref)
{
    if (!kref)
        return;
    kref->refcount = 1;
}

/* kref_get: Implement kref get. */
void kref_get(struct kref *kref)
{
    if (!kref)
        return;
    if (kref->refcount < 0xFFFFFFFF)
        kref->refcount++;
}

/* kref_put: Implement kref put. */
int kref_put(struct kref *kref, void (*release)(struct kref *kref))
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
