#ifndef LINUX_DEVICE_H
#define LINUX_DEVICE_H

#include <stdint.h>
#include "linux/fs.h"
#include "linux/kobject.h"

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

void driver_init(void);
struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *));
int driver_register(struct device_driver *drv);
int driver_bind_device(struct device_driver *drv, struct device *dev);
int device_register(struct device *dev);
int device_unregister(struct device *dev);
int device_unbind(struct device *dev);
int device_rebind(struct device *dev);

struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data);
struct bus_type *device_model_platform_bus(void);
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *));

uint32_t device_model_device_count(void);
struct device *device_model_device_at(uint32_t index);
struct device *device_model_find_device(const char *name);
struct kset *device_model_devices_kset(void);
struct kset *device_model_drivers_kset(void);
int device_model_inited(void);
void device_model_mark_inited(void);
void device_model_kset_init(void);
int bus_default_match(struct device *dev, struct device_driver *drv);

#endif
