#include "linux/device.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/devtmpfs.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static int devmodel_ready = 0;
static struct kset devices_kset;
static struct kset drivers_kset;

static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = CONTAINER_OF(kobj, struct device, kobj);
    kfree(dev);
}

int device_unbind(struct device *dev)
{
    if (!dev)
        return -1;
    if (!dev->driver)
        return 0;
    struct device_driver *drv = dev->driver;
    dev->driver = NULL;
    if (drv->remove)
        drv->remove(dev);
    return 0;
}

int device_rebind(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    struct device_driver *drv = dev->bus->drivers;
    while (drv) {
        if (dev->bus->match && dev->bus->match(dev, drv)) {
            dev->driver = drv;
            if (drv->probe)
                drv->probe(dev);
            return 0;
        }
        drv = drv->next;
    }
    return 0;
}

int device_register(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    kset_add(device_model_devices_kset(), &dev->kobj);
    dev->kobj.next = (struct kobject*)dev->bus->devices;
    dev->bus->devices = dev;
    devtmpfs_register_device(dev);
    device_rebind(dev);
    return 0;
}

int device_unregister(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    struct device *prev = NULL;
    struct device *cur = dev->bus->devices;
    while (cur) {
        if (cur == dev) {
            if (prev)
                prev->kobj.next = cur->kobj.next;
            else
                dev->bus->devices = (struct device*)cur->kobj.next;
            devtmpfs_unregister_device(dev);
            kset_remove(&devices_kset, &dev->kobj);
            kobject_put(&dev->kobj);
            return 0;
        }
        prev = cur;
        cur = (struct device*)cur->kobj.next;
    }
    return -1;
}

struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data)
{
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->driver_data = data;
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

uint32_t device_model_device_count(void)
{
    if (!devmodel_ready)
        return 0;
    uint32_t n = 0;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        n++;
        d = (struct device*)d->kobj.next;
    }
    return n;
}

struct device *device_model_device_at(uint32_t index)
{
    if (!devmodel_ready)
        return NULL;
    uint32_t i = 0;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        if (i == index)
            return d;
        i++;
        d = (struct device*)d->kobj.next;
    }
    return NULL;
}

struct device *device_model_find_device(const char *name)
{
    if (!devmodel_ready || !name)
        return NULL;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        if (!strcmp(d->kobj.name, name))
            return d;
        d = (struct device*)d->kobj.next;
    }
    return NULL;
}

struct kset *device_model_devices_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &devices_kset;
}

struct kset *device_model_drivers_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &drivers_kset;
}

int device_model_inited(void)
{
    return devmodel_ready;
}

void device_model_mark_inited(void)
{
    devmodel_ready = 1;
}

void device_model_kset_init(void)
{
    kset_init(&devices_kset, "devices");
    kset_init(&drivers_kset, "drivers");
}
