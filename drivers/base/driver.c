#include "linux/device.h"
#include "linux/libc.h"

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
            if (drv->probe)
                drv->probe(dev);
            return;
        }
        drv = drv->next;
    }
}

int driver_bind_device(struct device_driver *drv, struct device *dev)
{
    if (!drv || !dev || !drv->bus)
        return -1;
    if (dev->driver)
        device_unbind(dev);
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return -1;
    dev->driver = drv;
    if (drv->probe)
        drv->probe(dev);
    return 0;
}

int driver_register(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    kset_add(device_model_drivers_kset(), &drv->kobj);
    drv->next = drv->bus->drivers;
    drv->bus->drivers = drv;
    struct device *dev = drv->bus->devices;
    while (dev) {
        try_bind_device(drv->bus, dev);
        dev = (struct device*)dev->kobj.next;
    }
    return 0;
}

void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv)
        return;
    memset(drv, 0, sizeof(*drv));
    if (name) {
        uint32_t n = (uint32_t)strlen(name);
        if (n >= sizeof(drv->kobj.name))
            n = sizeof(drv->kobj.name) - 1;
        memcpy(drv->kobj.name, name, n);
        drv->kobj.name[n] = 0;
    }
    kref_init(&drv->kobj.kref);
    drv->bus = bus;
    drv->probe = probe;
}
