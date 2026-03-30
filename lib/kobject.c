#include "linux/kobject.h"
#include "linux/libc.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static void kobject_release(struct kref *kref)
{
    struct kobject *kobj = CONTAINER_OF(kref, struct kobject, kref);
    if (kobj->release)
        kobj->release(kobj);
}

void kobject_init(struct kobject *kobj, const char *name, void (*release)(struct kobject *))
{
    if (!kobj)
        return;
    memset(kobj, 0, sizeof(*kobj));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(kobj->name))
            n = sizeof(kobj->name) - 1;
        memcpy(kobj->name, name, n);
        kobj->name[n] = 0;
    }
    kref_init(&kobj->kref);
    kobj->ktype = NULL;
    kobj->release = release;
    INIT_LIST_HEAD(&kobj->entry);
}

struct kobject *kobject_get(struct kobject *kobj)
{
    if (!kobj)
        return NULL;
    kref_get(&kobj->kref);
    return kobj;
}

void kobject_put(struct kobject *kobj)
{
    if (!kobj)
        return;
    kref_put(&kobj->kref, kobject_release);
}

void kset_init(struct kset *kset, const char *name)
{
    if (!kset)
        return;
    kobject_init(&kset->kobj, name, NULL);
    INIT_LIST_HEAD(&kset->list);
}

void kset_add(struct kset *kset, struct kobject *kobj)
{
    if (!kset || !kobj)
        return;
    if (!kobj->entry.next || !kobj->entry.prev)
        INIT_LIST_HEAD(&kobj->entry);
    list_add_tail(&kobj->entry, &kset->list);
}

void kset_remove(struct kset *kset, struct kobject *kobj)
{
    if (!kset || !kobj)
        return;
    if (kobj->entry.next && kobj->entry.prev)
        list_del(&kobj->entry);
}
