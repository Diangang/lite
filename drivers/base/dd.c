#include "linux/device.h"
#include "linux/errno.h"
#include "linux/printk.h"
#include "linux/string.h"
#include "base.h"

/*
 * Linux mapping: drivers/base/dd.c hosts the core device/driver matching,
 * binding and deferred-probe plumbing. Lite keeps a synchronous subset.
 */

static LIST_HEAD(deferred_probe_pending_list);
static LIST_HEAD(deferred_probe_active_list);

void driver_deferred_probe_add(struct device *dev)
{
    if (!dev)
        return;
    if (!list_empty(&dev->deferred_probe))
        return;
    get_device(dev);
    list_add_tail(&dev->deferred_probe, &deferred_probe_pending_list);
}

void driver_deferred_probe_remove(struct device *dev)
{
    if (!dev)
        return;
    if (list_empty(&dev->deferred_probe))
        return;
    list_del_init(&dev->deferred_probe);
    put_device(dev);
}

void driver_deferred_probe_trigger(void)
{
    list_splice_tail_init(&deferred_probe_pending_list, &deferred_probe_active_list);

    while (!list_empty(&deferred_probe_active_list)) {
        struct device *dev = list_first_entry(&deferred_probe_active_list, struct device, deferred_probe);

        list_del_init(&dev->deferred_probe);
        if (!dev->driver)
            device_attach(dev);
        put_device(dev);
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
