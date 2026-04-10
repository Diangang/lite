#ifndef LINUX_DEVICE_H
#define LINUX_DEVICE_H

#include <stdint.h>
#include "linux/fs.h"
#include "linux/kobject.h"
#include "linux/list.h"

struct device;
struct device_driver;

struct device_attribute {
    struct attribute attr;
    uint32_t (*show)(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);
    uint32_t (*store)(struct device *dev, struct device_attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer);
};

struct driver_attribute {
    struct attribute attr;
    uint32_t (*show)(struct device_driver *drv, struct driver_attribute *attr, char *buffer, uint32_t cap);
    uint32_t (*store)(struct device_driver *drv, struct driver_attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer);
};

struct device {
    struct kobject kobj;
    struct bus_type *bus;
    struct device_driver *driver;
    struct class *class;
    struct list_head bus_list;
    struct list_head class_list;
    const char *type;
    uint32_t dev_major;
    uint32_t dev_minor;
    const char *devnode_name;
    void *driver_data;
    void (*release)(struct device *dev);
};

struct bus_type {
    struct kobject kobj;
    struct list_head list;
    struct list_head devices;
    struct list_head drivers;
    int (*match)(struct device *dev, struct device_driver *drv);
};

struct device_driver {
    struct kobject kobj;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    struct list_head bus_list;
};

struct class {
    struct kobject kobj;
    struct list_head list;
    struct list_head devices;
};

void driver_init(void);
void device_initialize(struct device *dev, const char *name);
struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *));
int bus_register_static(struct bus_type *bus);
uint32_t bus_count(void);
struct bus_type *bus_at(uint32_t index);
struct bus_type *bus_find(const char *name);
int driver_register(struct device_driver *drv);
int driver_unregister(struct device_driver *drv);
int driver_probe_device(struct device_driver *drv, struct device *dev);
int driver_attach(struct device_driver *drv);
int device_register(struct device *dev);
int device_unregister(struct device *dev);
int device_add(struct device *dev);
int device_del(struct device *dev);
int device_set_parent(struct device *dev, struct device *parent);
int device_unbind(struct device *dev);
int device_release_driver(struct device *dev);
int device_attach(struct device *dev);
int device_reprobe(struct device *dev);
int device_get_devpath(struct device *dev, char *buf, uint32_t cap);
int device_get_sysfs_path(struct device *dev, char *buf, uint32_t cap);
int device_get_modalias(struct device *dev, char *buf, uint32_t cap);

struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data);
struct device *device_register_simple_parent(const char *name, const char *type, struct bus_type *bus, struct device *parent, void *data);
struct device *device_register_simple_class(const char *name, const char *type, struct bus_type *bus, struct class *cls, void *data);
struct device *device_register_simple_class_parent(const char *name, const char *type, struct bus_type *bus, struct class *cls, struct device *parent, void *data);
struct bus_type *device_model_platform_bus(void);
struct bus_type *device_model_pci_bus(void);
struct device *device_model_platform_root(void);
void device_model_set_platform_root(struct device *dev);
struct device *device_model_pci_root(void);
void device_model_set_pci_root(struct device *dev);
struct device *device_model_virtual_root(void);
void device_model_set_virtual_root(struct device *dev);
struct device *device_model_virtual_subsys(const char *name);
struct class *device_model_console_class(void);
struct class *device_model_tty_class(void);
struct class *device_model_block_class(void);
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *));
int class_register(struct class *cls);
int class_unregister(struct class *cls);
struct class *class_find(const char *name);
void device_uevent_emit(const char *action, struct device *dev);
uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer);
void driver_deferred_probe_add(struct device *dev);
void driver_deferred_probe_trigger(void);

uint32_t device_model_device_count(void);
struct device *device_model_device_at(uint32_t index);
struct device *device_model_find_device(const char *name);
struct kset *device_model_devices_kset(void);
struct kset *device_model_drivers_kset(void);
struct kset *device_model_classes_kset(void);
struct kset *device_model_buses_kset(void);
void device_model_kset_init(void);
int bus_default_match(struct device *dev, struct device_driver *drv);

#endif
