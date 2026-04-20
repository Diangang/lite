#include "linux/serio.h"
#include "linux/device.h"
#include "linux/init.h"
#include "linux/kernel.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/vsprintf.h"

/*
 * Minimal serio -> driver core mapping (Linux reference):
 * - linux2.6/drivers/input/serio/serio.c: struct bus_type serio_bus + match/probe/remove
 * - linux2.6/include/linux/serio.h: struct serio embeds struct device; struct serio_driver embeds struct device_driver
 *
 * Lite simplifications:
 * - no kseriod/event queue; registration is synchronous via device_add()/driver_register()
 * - no manual_bind/bind_mode sysfs; rely on bus.drivers_autoprobe + driver core interfaces
 */

struct bus_type serio_bus;

static uint32_t serio_port_no;

static int serio_match_port(const struct serio_device_id *ids, struct serio *serio)
{
    if (!ids || !serio)
        return 0;
    while (ids->type || ids->proto || ids->id || ids->extra) {
        if ((ids->type == SERIO_ANY || ids->type == serio->id.type) &&
            (ids->proto == SERIO_ANY || ids->proto == serio->id.proto) &&
            (ids->extra == SERIO_ANY || ids->extra == serio->id.extra) &&
            (ids->id == SERIO_ANY || ids->id == serio->id.id))
            return 1;
        ids++;
    }
    return 0;
}

static int serio_bus_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    struct serio *serio = to_serio_port(dev);
    struct serio_driver *sdrv = to_serio_driver(drv);

    /* Linux-like: prefer id_table match; keep Lite legacy (NULL means accept any). */
    if (!sdrv->id_table)
        return 1;
    return serio_match_port(sdrv->id_table, serio);
}

static int serio_driver_probe(struct device *dev)
{
    if (!dev || !dev->driver)
        return -1;
    struct serio *serio = to_serio_port(dev);
    struct serio_driver *sdrv = to_serio_driver(dev->driver);
    if (!serio || !sdrv)
        return -1;
    if (sdrv->connect) {
        int rc = sdrv->connect(serio, sdrv);
        if (rc != 0)
            return rc;
    }
    /* Cache typed driver pointer for IRQ fast-path; cleared in ->remove(). */
    serio->drv = sdrv;
    return 0;
}

static void serio_driver_remove(struct device *dev)
{
    struct serio *serio = dev ? to_serio_port(dev) : NULL;
    if (!serio)
        return;
    /*
     * Lite driver core clears dev->driver before calling ->remove(), so use the
     * cached typed pointer.
     */
    struct serio_driver *sdrv = serio->drv;
    if (sdrv && sdrv->disconnect)
        sdrv->disconnect(serio);
    serio->drv = NULL;
}

static void serio_release_port(struct device *dev)
{
    struct serio *serio = dev ? to_serio_port(dev) : NULL;
    kfree(serio);
}

static void serio_init_port(struct serio *serio)
{
    if (!serio)
        return;
    char name[16];
    snprintf(name, sizeof(name), "serio%u", serio_port_no++);

    int caller_set_release = (serio->dev.release != NULL);
    device_initialize(&serio->dev, name);
    /*
     * Important: struct serio embeds struct device, so default device release
     * (kfree(dev)) is wrong. Default to freeing the container; static ports may
     * override dev.release with a no-op.
     */
    if (!caller_set_release)
        serio->dev.release = serio_release_port;
    serio->dev.bus = &serio_bus;
    if (serio->parent)
        device_set_parent(&serio->dev, serio->parent);
    serio->drv = NULL;
}

int serio_register_port(struct serio *serio)
{
    if (!serio)
        return -1;
    if (!serio->dev.kobj.name[0])
        serio_init_port(serio);
    serio->dev.bus = &serio_bus;
    return device_add(&serio->dev);
}

void serio_unregister_port(struct serio *serio)
{
    if (!serio)
        return;
    device_unregister(&serio->dev);
}

int serio_register_driver(struct serio_driver *drv)
{
    if (!drv || !drv->driver.name || !drv->driver.name[0])
        return -1;
    init_driver(&drv->driver, drv->driver.name, &serio_bus, serio_driver_probe);
    drv->driver.remove = serio_driver_remove;
    return driver_register(&drv->driver);
}

void serio_unregister_driver(struct serio_driver *drv)
{
    if (!drv)
        return;
    driver_unregister(&drv->driver);
}

void serio_interrupt(struct serio *serio, uint8_t data)
{
    if (!serio)
        return;
    struct serio_driver *drv = serio->drv;
    if (!drv) {
        (void)device_attach(&serio->dev);
        drv = serio->drv;
    }
    if (drv && drv->interrupt)
        drv->interrupt(serio, data);
}

static int serio_core_init(void)
{
    memset(&serio_bus, 0, sizeof(serio_bus));
    serio_bus.name = "serio";
    serio_bus.match = serio_bus_match;
    INIT_LIST_HEAD(&serio_bus.list);
    return bus_register(&serio_bus);
}
subsys_initcall(serio_core_init);
