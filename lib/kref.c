#include "linux/kref.h"

void kref_init(struct kref *kref)
{
    if (!kref)
        return;
    kref->refcount = 1;
}

void kref_get(struct kref *kref)
{
    if (!kref)
        return;
    if (kref->refcount < 0xFFFFFFFF)
        kref->refcount++;
}

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
