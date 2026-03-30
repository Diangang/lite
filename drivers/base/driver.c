#include "linux/device.h"
#include "linux/libc.h"

static void try_bind_device(struct bus_type *bus, struct device *dev)
{
    if (!bus || !dev)
        return;
    if (dev->driver)
        return;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (bus->match && bus->match(dev, drv)) {
            dev->driver = drv;
            if (drv->probe && drv->probe(dev) != 0) {
                dev->driver = NULL;
                continue;
            }
            device_uevent_emit("bind", dev);
            return;
        }
    }
}

int driver_bind_device(struct device_driver *drv, struct device *dev)
{
    if (!drv || !dev || !drv->bus)
        return -1;
    if (dev->driver == drv)
        return 0;
    if (dev->driver)
        device_unbind(dev);
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return -1;
    dev->driver = drv;
    if (drv->probe && drv->probe(dev) != 0) {
        dev->driver = NULL;
        return -1;
    }
    device_uevent_emit("bind", dev);
    return 0;
}

int driver_register(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    struct device_driver *cur;
    list_for_each_entry(cur, &drv->bus->drivers, bus_list) {
        if (!strcmp(cur->kobj.name, drv->kobj.name))
            return -1;
    }
    kset_add(device_model_drivers_kset(), &drv->kobj);
    INIT_LIST_HEAD(&drv->bus_list);
    list_add_tail(&drv->bus_list, &drv->bus->drivers);
    struct device *dev;
    list_for_each_entry(dev, &drv->bus->devices, bus_list) {
        try_bind_device(drv->bus, dev);
    }
    return 0;
}

int driver_unregister(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    struct device *dev;
    list_for_each_entry(dev, &drv->bus->devices, bus_list) {
        if (dev->driver == drv)
            device_unbind(dev);
    }
    if (drv->bus_list.next && drv->bus_list.prev)
        list_del(&drv->bus_list);
    kset_remove(device_model_drivers_kset(), &drv->kobj);
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

void init_driver_ids(struct device_driver *drv, const char *name, struct bus_type *bus, const struct device_id *id_table, int (*probe)(struct device *))
{
    init_driver(drv, name, bus, probe);
    if (drv)
        drv->id_table = id_table;
}
