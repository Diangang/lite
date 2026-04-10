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

void init_sysfs(void);

#endif
