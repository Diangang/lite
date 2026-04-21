#include "linux/device.h"
#include "linux/errno.h"
#include "base.h"

/*
 * Linux mapping: drivers/base/dd.c hosts the core device/driver matching,
 * binding and deferred-probe plumbing. Lite keeps a synchronous subset.
 */

static struct device *deferred_devs[64];
static uint32_t deferred_devs_count = 0;

static void driver_deferred_probe_remove_index(uint32_t idx)
{
    struct device *dev;

    if (idx >= deferred_devs_count)
        return;
    dev = deferred_devs[idx];
    if (dev)
        put_device(dev);
    for (uint32_t j = idx + 1; j < deferred_devs_count; j++)
        deferred_devs[j - 1] = deferred_devs[j];
    deferred_devs_count--;
}

void driver_deferred_probe_add(struct device *dev)
{
    if (!dev)
        return;
    for (uint32_t i = 0; i < deferred_devs_count; i++) {
        if (deferred_devs[i] == dev)
            return;
    }
    if (deferred_devs_count < (uint32_t)(sizeof(deferred_devs) / sizeof(deferred_devs[0])))
        deferred_devs[deferred_devs_count++] = get_device(dev);
}

void driver_deferred_probe_remove(struct device *dev)
{
    if (!dev)
        return;
    for (uint32_t i = 0; i < deferred_devs_count;) {
        if (deferred_devs[i] == dev) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        i++;
    }
}

void driver_deferred_probe_trigger(void)
{
    for (uint32_t i = 0; i < deferred_devs_count;) {
        struct device *dev = deferred_devs[i];
        if (!dev || dev->driver) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        device_attach(dev);
        if (dev->driver) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        i++;
    }
}

int driver_probe_device(struct device_driver *drv, struct device *dev)
{
    int rc;

    if (!drv || !dev || !drv->bus)
        return -1;
    if (dev->driver == drv)
        return 0;
    if (dev->driver)
        return -1;
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return -1;

    kobject_get(&drv->kobj);
    dev->driver = drv;
    rc = drv->probe ? drv->probe(dev) : 0;
    if (rc != 0) {
        sysfs_remove_link(&dev->kobj, "driver");
        dev->driver = NULL;
        kobject_put(&drv->kobj);
        if (rc == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        return -1;
    }
    sysfs_create_link(&drv->kobj, &dev->kobj, dev->kobj.name);
    sysfs_create_link(&dev->kobj, &drv->kobj, "driver");
    driver_deferred_probe_remove(dev);
    device_uevent_emit("bind", dev);
    return 0;
}

int driver_bind_device(struct device_driver *drv, struct device *dev)
{
    return driver_probe_device(drv, dev);
}

int driver_attach(struct device_driver *drv)
{
    struct klist_iter iter;
    struct klist_node *node;

    if (!drv || !drv->bus)
        return -1;
    klist_iter_init(bus_devices_klist(drv->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = container_of(node, struct device, knode_bus);
        int rc;

        if (dev->driver)
            continue;
        rc = driver_probe_device(drv, dev);
        if (rc == -EPROBE_DEFER)
            driver_deferred_probe_add(dev);
    }
    klist_iter_exit(&iter);
    return 0;
}
