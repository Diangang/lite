#ifndef LINUX_SYSFS_H
#define LINUX_SYSFS_H

#include <stdint.h>
#include "linux/fs.h"

struct kobject;

struct attribute {
    const char *name;
    uint32_t mode;
};

struct attribute_group {
    const char *name;
    const struct attribute **attrs;
    uint32_t (*is_visible)(struct kobject *kobj, const struct attribute *attr);
};

struct sysfs_ops {
    uint32_t (*show)(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap);
    uint32_t (*store)(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer);
};

int sysfs_init(void);
void sysfs_mount(void);
int sysfs_create_dir(struct kobject *kobj);
int sysfs_create_file(struct kobject *kobj, const struct attribute *attr);
int sysfs_create_subdir(struct kobject *kobj, const char *name, uint32_t mode);
int sysfs_create_link(struct kobject *kobj, struct kobject *target, const char *name);
void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr);
void sysfs_remove_subdir(struct kobject *kobj, const char *name);
void sysfs_remove_link(struct kobject *kobj, const char *name);
void sysfs_remove_dir(struct kobject *kobj);

#endif
