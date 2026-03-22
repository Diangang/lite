#ifndef DEVICE_MODEL_H
#define DEVICE_MODEL_H

#include <stdint.h>
#include "vfs.h"

struct kobject {
    char name[32];
    struct kobject *parent;
    struct kobject *children;
    struct kobject *next;
    uint32_t refcount;
    void (*release)(struct kobject *kobj);
};

struct device {
    struct kobject kobj;
    struct bus_type *bus;
    struct device_driver *driver;
    const char *type;
    void *driver_data;
};

struct bus_type {
    struct kobject kobj;
    struct device *devices;
    struct device_driver *drivers;
    int (*match)(struct device *dev, struct device_driver *drv);
    struct bus_type *next;
};

struct device_driver {
    struct kobject kobj;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    struct device_driver *next;
};

void device_model_init(struct vfs_inode *ram_root, struct vfs_inode *initrd_root);
struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *));
int driver_register(struct device_driver *drv);
int device_register(struct device *dev);
int device_unbind(struct device *dev);
int device_rebind(struct device *dev);

struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data);
struct bus_type *device_model_platform_bus(void);

uint32_t device_model_device_count(void);
struct device *device_model_device_at(uint32_t index);
struct device *device_model_find_device(const char *name);

#endif
