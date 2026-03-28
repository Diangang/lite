#ifndef LINUX_KOBJECT_H
#define LINUX_KOBJECT_H

#include "linux/kref.h"

struct kobject {
    char name[32];
    struct kobject *parent;
    struct kobject *children;
    struct kobject *next;
    struct kref kref;
    struct kobj_type *ktype;
    void (*release)(struct kobject *kobj);
};

struct kobj_type {
    void (*release)(struct kobject *kobj);
};

struct kset {
    struct kobject kobj;
    struct kobject *list;
};

void kobject_init(struct kobject *kobj, const char *name, void (*release)(struct kobject *));
struct kobject *kobject_get(struct kobject *kobj);
void kobject_put(struct kobject *kobj);
void kset_init(struct kset *kset, const char *name);
void kset_add(struct kset *kset, struct kobject *kobj);
void kset_remove(struct kset *kset, struct kobject *kobj);

#endif
