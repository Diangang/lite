#include "device_model.h"
#include "kheap.h"
#include "libc.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static void kobject_init(kobject_t *kobj, const char *name, void (*release)(kobject_t *))
{
    if (!kobj) return;
    memset(kobj, 0, sizeof(*kobj));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(kobj->name)) n = sizeof(kobj->name) - 1;
        memcpy(kobj->name, name, n);
        kobj->name[n] = 0;
    }
    kobj->refcount = 1;
    kobj->release = release;
}

static void kobject_put(kobject_t *kobj)
{
    if (!kobj) return;
    if (kobj->refcount > 0) kobj->refcount--;
    if (kobj->refcount > 0) return;
    if (kobj->release) kobj->release(kobj);
}

static bus_type_t *bus_list = NULL;
static bus_type_t platform_bus;
static int devmodel_inited = 0;

static void device_release_kobj(kobject_t *kobj)
{
    device_t *dev = CONTAINER_OF(kobj, device_t, kobj);
    kfree(dev);
}

static void bus_release_kobj(kobject_t *kobj)
{
    bus_type_t *bus = CONTAINER_OF(kobj, bus_type_t, kobj);
    kfree(bus);
}

static int default_match(device_t *dev, device_driver_t *drv)
{
    if (!dev || !drv) return 0;
    if (!dev->type) return 0;
    if (!drv->kobj.name[0]) return 0;
    return strcmp(dev->type, drv->kobj.name) == 0;
}

bus_type_t *bus_register(const char *name, int (*match)(device_t *, device_driver_t *))
{
    bus_type_t *bus = (bus_type_t*)kmalloc(sizeof(bus_type_t));
    if (!bus) return NULL;
    memset(bus, 0, sizeof(*bus));
    kobject_init(&bus->kobj, name, bus_release_kobj);
    bus->match = match ? match : default_match;
    bus->next = bus_list;
    bus_list = bus;
    return bus;
}

static void try_bind_device(bus_type_t *bus, device_t *dev)
{
    if (!bus || !dev) return;
    if (dev->driver) return;
    device_driver_t *drv = bus->drivers;
    while (drv) {
        if (bus->match && bus->match(dev, drv)) {
            dev->driver = drv;
            if (drv->probe) drv->probe(dev);
            return;
        }
        drv = drv->next;
    }
}

int device_unbind(device_t *dev)
{
    if (!dev) return -1;
    if (!dev->driver) return 0;
    device_driver_t *drv = dev->driver;
    dev->driver = NULL;
    if (drv->remove) drv->remove(dev);
    return 0;
}

int device_rebind(device_t *dev)
{
    if (!dev || !dev->bus) return -1;
    device_unbind(dev);
    try_bind_device(dev->bus, dev);
    return 0;
}

int driver_register(device_driver_t *drv)
{
    if (!drv || !drv->bus) return -1;
    drv->next = drv->bus->drivers;
    drv->bus->drivers = drv;
    device_t *dev = drv->bus->devices;
    while (dev) {
        try_bind_device(drv->bus, dev);
        dev = (device_t*)dev->kobj.next;
    }
    return 0;
}

int device_register(device_t *dev)
{
    if (!dev || !dev->bus) return -1;
    dev->kobj.next = (kobject_t*)dev->bus->devices;
    dev->bus->devices = dev;
    try_bind_device(dev->bus, dev);
    return 0;
}

device_t *device_register_simple(const char *name, const char *type, bus_type_t *bus, void *data)
{
    if (!bus) return NULL;
    device_t *dev = (device_t*)kmalloc(sizeof(device_t));
    if (!dev) return NULL;
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

bus_type_t *device_model_platform_bus(void)
{
    if (!devmodel_inited) return NULL;
    return &platform_bus;
}

void device_model_init(void)
{
    if (devmodel_inited) return;
    memset(&platform_bus, 0, sizeof(platform_bus));
    kobject_init(&platform_bus.kobj, "platform", NULL);
    platform_bus.match = default_match;
    platform_bus.devices = NULL;
    platform_bus.drivers = NULL;
    platform_bus.next = NULL;
    bus_list = &platform_bus;
    devmodel_inited = 1;
}

uint32_t device_model_device_count(void)
{
    if (!devmodel_inited) return 0;
    uint32_t n = 0;
    device_t *d = platform_bus.devices;
    while (d) {
        n++;
        d = (device_t*)d->kobj.next;
    }
    return n;
}

device_t *device_model_device_at(uint32_t index)
{
    if (!devmodel_inited) return NULL;
    uint32_t i = 0;
    device_t *d = platform_bus.devices;
    while (d) {
        if (i == index) return d;
        i++;
        d = (device_t*)d->kobj.next;
    }
    return NULL;
}

device_t *device_model_find_device(const char *name)
{
    if (!devmodel_inited || !name) return NULL;
    device_t *d = platform_bus.devices;
    while (d) {
        if (!strcmp(d->kobj.name, name)) return d;
        d = (device_t*)d->kobj.next;
    }
    return NULL;
}
