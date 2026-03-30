#ifndef LINUX_DEVICE_H
#define LINUX_DEVICE_H

#include <stdint.h>
#include "linux/fs.h"
#include "linux/kobject.h"
#include "linux/list.h"

struct device {
    struct kobject kobj;
    struct bus_type *bus;
    struct device_driver *driver;
    struct class *class;
    struct list_head bus_list;
    struct list_head class_list;
    const char *type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass_id;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t bus_num;
    uint8_t dev_num;
    uint8_t func_num;
    uint32_t bar[6];
    uint32_t bar_size[6];
    uint32_t io_base;
    uint32_t io_limit;
    uint32_t mem_base;
    uint32_t mem_limit;
    uint64_t pref_base;
    uint64_t pref_limit;
    void *driver_data;
};

struct bus_type {
    struct kobject kobj;
    struct list_head list;
    struct list_head devices;
    struct list_head drivers;
    int (*match)(struct device *dev, struct device_driver *drv);
};

struct device_id {
    const char *name;
    const char *type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass_id;
};

struct device_driver {
    struct kobject kobj;
    struct bus_type *bus;
    const struct device_id *id_table;
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
struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *));
int driver_register(struct device_driver *drv);
int driver_unregister(struct device_driver *drv);
int driver_bind_device(struct device_driver *drv, struct device *dev);
int device_register(struct device *dev);
int device_unregister(struct device *dev);
int device_set_parent(struct device *dev, struct device *parent);
int device_unbind(struct device *dev);
int device_rebind(struct device *dev);

struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data);
struct device *device_register_simple_class(const char *name, const char *type, struct bus_type *bus, struct class *cls, void *data);
struct device *device_register_simple_full(const char *name, const char *type, struct bus_type *bus, struct class *cls, void *data, uint16_t vendor_id, uint16_t device_id, uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, uint8_t revision, uint8_t bus_num, uint8_t dev_num, uint8_t func_num);
struct bus_type *device_model_platform_bus(void);
struct bus_type *device_model_pci_bus(void);
struct device *device_model_platform_root(void);
void device_model_set_platform_root(struct device *dev);
struct class *device_model_console_class(void);
struct class *device_model_tty_class(void);
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *));
void init_driver_ids(struct device_driver *drv, const char *name, struct bus_type *bus, const struct device_id *id_table, int (*probe)(struct device *));
int class_register(struct class *cls);
int class_unregister(struct class *cls);
struct class *class_find(const char *name);
void device_uevent_emit(const char *action, struct device *dev);
uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer);

uint32_t device_model_device_count(void);
struct device *device_model_device_at(uint32_t index);
struct device *device_model_find_device(const char *name);
struct kset *device_model_devices_kset(void);
struct kset *device_model_drivers_kset(void);
struct kset *device_model_classes_kset(void);
int device_model_inited(void);
void device_model_mark_inited(void);
void device_model_kset_init(void);
int bus_default_match(struct device *dev, struct device_driver *drv);

#endif
