#include "linux/device_model.h"
#include "linux/kheap.h"
#include "linux/libc.h"
#include "linux/fs.h"
#include "linux/devtmpfs.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static void kobject_init(struct kobject *kobj, const char *name, void (*release)(struct kobject *))
{
    if (!kobj)
        return;
    memset(kobj, 0, sizeof(*kobj));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(kobj->name))
            n = sizeof(kobj->name) - 1;
        memcpy(kobj->name, name, n);
        kobj->name[n] = 0;
    }
    kobj->refcount = 1;
    kobj->release = release;
}

static void kobject_put(struct kobject *kobj)
{
    if (!kobj)
        return;
    if (kobj->refcount > 0) kobj->refcount--;
    if (kobj->refcount > 0)
        return;
    if (kobj->release) kobj->release(kobj);
}

static struct bus_type *bus_list = NULL;
static struct bus_type platform_bus;
static int devmodel_inited = 0;

static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = CONTAINER_OF(kobj, struct device, kobj);
    kfree(dev);
}

static void bus_release_kobj(struct kobject *kobj)
{
    struct bus_type *bus = CONTAINER_OF(kobj, struct bus_type, kobj);
    kfree(bus);
}

static int default_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    if (!dev->type)
        return 0;
    if (!drv->kobj.name[0])
        return 0;
    return strcmp(dev->type, drv->kobj.name) == 0;
}

struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *))
{
    struct bus_type *bus = (struct bus_type*)kmalloc(sizeof(struct bus_type));
    if (!bus)
        return NULL;
    memset(bus, 0, sizeof(*bus));
    kobject_init(&bus->kobj, name, bus_release_kobj);
    bus->match = match ? match : default_match;
    bus->next = bus_list;
    bus_list = bus;
    return bus;
}

static void try_bind_device(struct bus_type *bus, struct device *dev)
{
    if (!bus || !dev)
        return;
    if (dev->driver)
        return;
    struct device_driver *drv = bus->drivers;
    while (drv) {
        if (bus->match && bus->match(dev, drv)) {
            dev->driver = drv;
            if (drv->probe) drv->probe(dev);
            return;
        }
        drv = drv->next;
    }
}

int device_unbind(struct device *dev)
{
    if (!dev)
        return -1;
    if (!dev->driver)
        return 0;
    struct device_driver *drv = dev->driver;
    dev->driver = NULL;
    if (drv->remove) drv->remove(dev);
    return 0;
}

int device_rebind(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    try_bind_device(dev->bus, dev);
    return 0;
}

int driver_register(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    drv->next = drv->bus->drivers;
    drv->bus->drivers = drv;
    struct device *dev = drv->bus->devices;
    while (dev) {
        try_bind_device(drv->bus, dev);
        dev = (struct device*)dev->kobj.next;
    }
    return 0;
}

int device_register(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    dev->kobj.next = (struct kobject*)dev->bus->devices;
    dev->bus->devices = dev;
    devtmpfs_register_device(dev);
    try_bind_device(dev->bus, dev);
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

struct bus_type *device_model_platform_bus(void)
{
    if (!devmodel_inited)
        return NULL;
    return &platform_bus;
}

void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv)
        return;
    memset(drv, 0, sizeof(*drv));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(drv->kobj.name)) n = sizeof(drv->kobj.name) - 1;
        memcpy(drv->kobj.name, name, n);
        drv->kobj.name[n] = 0;
    }
    drv->kobj.refcount = 1;
    drv->bus = bus;
    drv->probe = probe;
}

uint32_t device_model_device_count(void)
{
    if (!devmodel_inited)
        return 0;
    uint32_t n = 0;
    struct device *d = platform_bus.devices;
    while (d) {
        n++;
        d = (struct device*)d->kobj.next;
    }
    return n;
}

struct device *device_model_device_at(uint32_t index)
{
    if (!devmodel_inited)
        return NULL;
    uint32_t i = 0;
    struct device *d = platform_bus.devices;
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
    if (!devmodel_inited || !name)
        return NULL;
    struct device *d = platform_bus.devices;
    while (d) {
        if (!strcmp(d->kobj.name, name))
            return d;
        d = (struct device*)d->kobj.next;
    }
    return NULL;
}

void driver_init(void)
{
    if (devmodel_inited)
        return;
    memset(&platform_bus, 0, sizeof(platform_bus));
    kobject_init(&platform_bus.kobj, "platform", NULL);
    platform_bus.match = default_match;
    platform_bus.devices = NULL;
    platform_bus.drivers = NULL;
    platform_bus.next = NULL;
    bus_list = &platform_bus;
    devmodel_inited = 1;

    printf("Driver core initialized.\n");
}
