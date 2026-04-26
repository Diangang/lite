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

/* #region debug-point deferred-probe-nvme-dd */
static uint32_t deferred_probe_list_count(struct list_head *head)
{
    struct list_head *pos;
    uint32_t count = 0;

    if (!head)
        return 0;
    list_for_each(pos, head)
        count++;
    return count;
}

static const char *dbg_bus_name(struct device *dev)
{
    if (!dev || !dev->bus || !dev->bus->subsys.kset.kobj.name)
        return "-";
    return dev->bus->subsys.kset.kobj.name;
}

static const char *dbg_drv_name(struct device_driver *drv)
{
    if (!drv || !drv->name)
        return "-";
    return drv->name;
}

static void dbg_deferred_event(const char *ev, struct device *dev, struct device_driver *drv, int rc)
{
    printf("TRAEDBG {\"ev\":\"%s\",\"dev\":\"%s\",\"bus\":\"%s\",\"drv\":\"%s\",\"rc\":%d,\"pend\":%u,\"act\":%u}\n",
           ev ? ev : "-",
           (dev && dev->kobj.name[0]) ? dev->kobj.name : "-",
           dbg_bus_name(dev),
           dbg_drv_name(drv ? drv : (dev ? dev->driver : NULL)),
           rc,
           deferred_probe_list_count(&deferred_probe_pending_list),
           deferred_probe_list_count(&deferred_probe_active_list));
}
/* #endregion debug-point deferred-probe-nvme-dd */

void driver_deferred_probe_add(struct device *dev)
{
    if (!dev)
        return;
    if (!list_empty(&dev->deferred_probe))
        return;
    get_device(dev);
    list_add_tail(&dev->deferred_probe, &deferred_probe_pending_list);
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("defer_add", dev, NULL, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
}

void driver_deferred_probe_remove(struct device *dev)
{
    if (!dev)
        return;
    if (list_empty(&dev->deferred_probe))
        return;
    list_del_init(&dev->deferred_probe);
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("defer_del", dev, NULL, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
    put_device(dev);
}

void driver_deferred_probe_trigger(void)
{
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("defer_trigger_begin", NULL, NULL, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
    list_splice_tail_init(&deferred_probe_pending_list, &deferred_probe_active_list);
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("defer_trigger_spliced", NULL, NULL, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */

    while (!list_empty(&deferred_probe_active_list)) {
        struct device *dev = list_first_entry(&deferred_probe_active_list, struct device, deferred_probe);

        list_del_init(&dev->deferred_probe);
        /* #region debug-point deferred-probe-nvme-dd */
        dbg_deferred_event("defer_retry", dev, NULL, 0);
        /* #endregion debug-point deferred-probe-nvme-dd */
        if (!dev->driver)
            device_attach(dev);
        put_device(dev);
    }
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("defer_trigger_end", NULL, NULL, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
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

    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("probe_enter", dev, drv, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
    kobject_get(&drv->kobj);
    dev->driver = drv;
    rc = drv->probe ? drv->probe(dev) : 0;
    if (rc != 0) {
        sysfs_remove_link(&dev->kobj, "driver");
        dev->driver = NULL;
        kobject_put(&drv->kobj);
        /* #region debug-point deferred-probe-nvme-dd */
        dbg_deferred_event("probe_fail", dev, drv, rc);
        /* #endregion debug-point deferred-probe-nvme-dd */
        if (rc == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        return -1;
    }
    sysfs_create_link(&drv->kobj, &dev->kobj, dev->kobj.name);
    sysfs_create_link(&dev->kobj, &drv->kobj, "driver");
    driver_deferred_probe_remove(dev);
    /* #region debug-point deferred-probe-nvme-dd */
    dbg_deferred_event("probe_ok", dev, drv, 0);
    /* #endregion debug-point deferred-probe-nvme-dd */
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
