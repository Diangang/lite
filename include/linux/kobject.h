#ifndef LINUX_KOBJECT_H
#define LINUX_KOBJECT_H

#include "sysfs.h"
#include "kref.h"
#include "list.h"

struct sysfs_dirent;

struct kobject {
    char name[32];
    struct kobject *parent;
    struct kobject *children;
    struct kobject *next;
    struct kset *kset;
    struct list_head entry;
    struct kref kref;
    struct kobj_type *ktype;
    struct sysfs_dirent *sd;
    void (*release)(struct kobject *kobj);
};

struct kobj_type {
    void (*release)(struct kobject *kobj);
    const struct sysfs_ops *sysfs_ops;
    const struct attribute **default_attrs;
    const struct attribute_group **default_groups;
};

struct kset {
    struct kobject kobj;
    struct list_head list;
};

/*
 * Linux alignment note:
 * Linux 2.6 driver core roots are commonly modeled as struct subsystem with an
 * embedded kset. Lite keeps only the embedded kset shape for now and does not
 * implement the full rwsem/lifetime semantics yet.
 */
struct subsystem {
    struct kset kset;
};

void kobject_init(struct kobject *kobj, const char *name, void (*release)(struct kobject *));
void kobject_init_with_ktype(struct kobject *kobj, const char *name, struct kobj_type *ktype,
                             void (*release)(struct kobject *));
int kobject_add(struct kobject *kobj);
void kobject_del(struct kobject *kobj);
struct kobject *kobject_get(struct kobject *kobj);
void kobject_put(struct kobject *kobj);
void kset_init(struct kset *kset, const char *name);
void kset_add(struct kset *kset, struct kobject *kobj);
void kset_remove(struct kset *kset, struct kobject *kobj);
int subsystem_register(struct subsystem *subsys);
void subsystem_unregister(struct subsystem *subsys);

#endif
