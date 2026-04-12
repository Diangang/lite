#include "linux/kobject.h"
#include "linux/kernel.h"
#include "linux/libc.h"

/* kobject_release: Implement kobject release. */
static void kobject_release(struct kref *kref)
{
    struct kobject *kobj = container_of(kref, struct kobject, kref);
    if (kobj->release)
        kobj->release(kobj);
}

/* kobject_init: Initialize kobject. */
void kobject_init(struct kobject *kobj, const char *name, void (*release)(struct kobject *))
{
    kobject_init_with_ktype(kobj, name, NULL, release);
}

void kobject_init_with_ktype(struct kobject *kobj, const char *name, struct kobj_type *ktype,
                             void (*release)(struct kobject *))
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
    kobj->kset = NULL;
    kobj->ktype = ktype;
    kobj->sd = NULL;
    kobj->release = release ? release : (ktype ? ktype->release : NULL);
    INIT_LIST_HEAD(&kobj->entry);
}

static struct kobject *kobject_parent(struct kobject *kobj)
{
    if (!kobj)
        return NULL;
    if (kobj->parent)
        return kobj->parent;
    if (kobj->kset && &kobj->kset->kobj != kobj)
        return &kobj->kset->kobj;
    return NULL;
}

static int kobject_child_linked(struct kobject *parent, struct kobject *child)
{
    if (!parent || !child)
        return 0;
    struct kobject *cur = parent->children;
    while (cur) {
        if (cur == child)
            return 1;
        cur = cur->next;
    }
    return 0;
}

int kobject_add(struct kobject *kobj)
{
    if (!kobj)
        return -1;
    struct kobject *parent = kobject_parent(kobj);
    if (parent && !kobject_child_linked(parent, kobj)) {
        kobj->next = parent->children;
        parent->children = kobj;
    }
    return sysfs_create_dir(kobj);
}

/* kobject_get: Implement kobject get. */
struct kobject *kobject_get(struct kobject *kobj)
{
    if (!kobj)
        return NULL;
    kref_get(&kobj->kref);
    return kobj;
}

/* kobject_put: Implement kobject put. */
void kobject_put(struct kobject *kobj)
{
    if (!kobj)
        return;
    kref_put(&kobj->kref, kobject_release);
}

/* kset_init: Initialize kset. */
void kset_init(struct kset *kset, const char *name)
{
    if (!kset)
        return;
    kobject_init(&kset->kobj, name, NULL);
    INIT_LIST_HEAD(&kset->list);
}

/* kset_add: Implement kset add. */
void kset_add(struct kset *kset, struct kobject *kobj)
{
    if (!kset || !kobj)
        return;
    kobj->kset = kset;
    if (!kobj->entry.next || !kobj->entry.prev)
        INIT_LIST_HEAD(&kobj->entry);
    list_add_tail(&kobj->entry, &kset->list);
    (void)kobject_add(kobj);
}

/* kset_remove: Implement kset remove. */
void kset_remove(struct kset *kset, struct kobject *kobj)
{
    if (!kset || !kobj)
        return;
    if (kobj->entry.next && kobj->entry.prev)
        list_del(&kobj->entry);
}

int subsystem_register(struct subsystem *subsys)
{
    if (!subsys)
        return -1;
    if (subsys->kset.kobj.kset) {
        if (!subsys->kset.kobj.entry.next || !subsys->kset.kobj.entry.prev)
            INIT_LIST_HEAD(&subsys->kset.kobj.entry);
        if (!subsys->kset.kobj.entry.next || !subsys->kset.kobj.entry.prev)
            return -1;
        list_add_tail(&subsys->kset.kobj.entry, &subsys->kset.kobj.kset->list);
    }
    return kobject_add(&subsys->kset.kobj);
}

void subsystem_unregister(struct subsystem *subsys)
{
    if (!subsys)
        return;
    if (subsys->kset.kobj.entry.next && subsys->kset.kobj.entry.prev)
        list_del(&subsys->kset.kobj.entry);
    sysfs_remove_dir(&subsys->kset.kobj);
}
