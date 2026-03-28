#ifndef LINUX_KREF_H
#define LINUX_KREF_H

#include <stdint.h>

struct kref {
    uint32_t refcount;
};

void kref_init(struct kref *kref);
void kref_get(struct kref *kref);
int kref_put(struct kref *kref, void (*release)(struct kref *kref));

#endif
