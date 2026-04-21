#ifndef LINUX_DEVICE_H
#define LINUX_DEVICE_H

#include <stdint.h>
#include "linux/fs.h"
#include "linux/kdev_t.h"
#include "linux/kobject.h"
#include "linux/klist.h"
#include "linux/list.h"

struct device;
struct bus_type;
struct class;
struct device_driver;
struct device_type;
struct subsys_private;

struct device_type {
    const char *name;
    const struct attribute_group **groups;
    /*
     * Linux mapping: device_get_devnode() can provide both the node name and
     * mode/uid/gid metadata for devtmpfs.
     */
    const char *(*devnode)(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid);
};

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

struct bus_attribute {
    struct attribute attr;
    uint32_t (*show)(struct bus_type *bus, struct bus_attribute *attr, char *buffer, uint32_t cap);
    uint32_t (*store)(struct bus_type *bus, struct bus_attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer);
};

struct class_attribute {
    struct attribute attr;
    uint32_t (*show)(struct class *cls, struct class_attribute *attr, char *buffer, uint32_t cap);
    uint32_t (*store)(struct class *cls, struct class_attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer);
};

struct device {
    struct kobject kobj;
    struct bus_type *bus;
    struct device_driver *driver;
    struct class *class;
    struct klist_node knode_bus;
    struct list_head class_list;
    const struct attribute_group **groups;
    const struct device_type *type;
    dev_t devt;
    void *driver_data;
    void (*release)(struct device *dev);
};

struct bus_type {
    const char *name;
    struct subsystem subsys;
    struct list_head list;
    const struct attribute_group **dev_groups;
    int (*match)(struct device *dev, struct device_driver *drv);
    struct subsys_private *p;
};

struct device_driver {
    const char *name;
    struct kobject kobj;
    struct bus_type *bus;
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    struct klist klist_devices;
    struct klist_node knode_bus;
};

struct class {
    const char *name;
    struct subsystem subsys;
    struct list_head list;
    struct list_head devices;
    const struct attribute_group **dev_groups;
    /* Linux mapping: class->devnode controls devtmpfs node name/mode/uid/gid. */
    const char *(*devnode)(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid);
    struct class_attribute *class_attrs;
};

void driver_init(void);
void device_initialize(struct device *dev, const char *name);
int bus_register(struct bus_type *bus);
void bus_unregister(struct bus_type *bus);
int bus_rescan_devices(struct bus_type *bus);
int driver_register(struct device_driver *drv);
void driver_unregister(struct device_driver *drv);
int driver_probe_device(struct device_driver *drv, struct device *dev);
int driver_attach(struct device_driver *drv);
int device_register(struct device *dev);
void device_unregister(struct device *dev);
int device_add(struct device *dev);
int device_del(struct device *dev);
int device_for_each_child(struct device *dev, void *data, int (*fn)(struct device *child, void *data));
int device_set_parent(struct device *dev, struct device *parent);
int device_unbind(struct device *dev);
int device_release_driver(struct device *dev);
int device_attach(struct device *dev);
int device_reprobe(struct device *dev);
int device_get_devpath(struct device *dev, char *buf, uint32_t cap);
int device_get_sysfs_path(struct device *dev, char *buf, uint32_t cap);
int device_get_modalias(struct device *dev, char *buf, uint32_t cap);
const char *device_get_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid);
int devtmpfs_create_node(struct device *dev);
int devtmpfs_delete_node(struct device *dev);
int devtmpfs_mount(const char *mntdir);

/* Linux mapping: drivers/base/core.c uses get_device/put_device wrappers. */
static inline struct device *get_device(struct device *dev)
{
    if (dev)
        kobject_get(&dev->kobj);
    return dev;
}

static inline void put_device(struct device *dev)
{
    if (dev)
        kobject_put(&dev->kobj);
}

void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *));
int class_register(struct class *cls);
void class_unregister(struct class *cls);
int class_create_file(struct class *cls, const struct class_attribute *attr);
void class_remove_file(struct class *cls, const struct class_attribute *attr);
void device_uevent_emit(const char *action, struct device *dev);
uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t device_uevent_seqnum(void);
void driver_deferred_probe_add(struct device *dev);
void driver_deferred_probe_remove(struct device *dev);
void driver_deferred_probe_trigger(void);
int bus_default_match(struct device *dev, struct device_driver *drv);

/* Linux mapping: drivers/base/core.c:device_create() */
struct device *device_create(struct class *cls, struct device *parent, dev_t devt, void *drvdata, const char *fmt, ...);

#endif
