#include "linux/platform_device.h"
#include "linux/kernel.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/vsprintf.h"
#include "base.h"

struct bus_type platform_bus_type;
struct device platform_bus;

static void platform_bus_release(struct device *dev)
{
    (void)dev;
}

int platform_bus_init(void)
{
    memset(&platform_bus, 0, sizeof(platform_bus));
    device_initialize(&platform_bus, "platform");
    platform_bus.release = platform_bus_release;
    if (device_add(&platform_bus) != 0)
        return -1;
    memset(&platform_bus_type, 0, sizeof(platform_bus_type));
    platform_bus_type.name = "platform";
    platform_bus_type.match = platform_bus_match;
    INIT_LIST_HEAD(&platform_bus_type.list);
    return bus_register_static(&platform_bus_type);
}

static void platform_device_release(struct device *dev)
{
    struct platform_device *pdev = container_of(dev, struct platform_device, dev);
    kfree(pdev);
}

struct platform_device *platform_get_platform_device(struct device *dev)
{
    if (!dev || !dev->driver_data)
        return NULL;
    struct platform_device *pdev = (struct platform_device *)dev->driver_data;
    if (&pdev->dev != dev)
        return NULL;
    return pdev;
}

static int platform_match_one(const struct platform_device_id *id, struct platform_device *pdev)
{
    if (!id || !pdev || !id->name || !pdev->name)
        return 0;
    return strcmp(id->name, pdev->name) == 0;
}

int platform_bus_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;

    /* First try platform_driver matching against platform_device_id. */
    struct platform_device *pdev = platform_get_platform_device(dev);
    if (pdev) {
        struct platform_driver *pdrv = to_platform_driver(drv);
        if (pdrv && pdrv->id_table) {
            const struct platform_device_id *id = pdrv->id_table;
            while (id->name) {
                if (platform_match_one(id, pdev))
                    return 1;
                id++;
            }
            return 0;
        }
    }

    /* Fallback: keep the old Lite behavior for non-platform_device objects. */
    return bus_default_match(dev, drv);
}

static int platform_driver_probe(struct device *dev)
{
    if (!dev || !dev->driver)
        return -1;
    struct platform_device *pdev = platform_get_platform_device(dev);
    if (!pdev)
        return -1;
    struct platform_driver *pdrv = to_platform_driver(dev->driver);
    if (!pdrv || !pdrv->probe)
        return 0;
    return pdrv->probe(pdev);
}

static void platform_driver_remove(struct device *dev)
{
    if (!dev || !dev->driver)
        return;
    struct platform_device *pdev = platform_get_platform_device(dev);
    if (!pdev)
        return;
    struct platform_driver *pdrv = to_platform_driver(dev->driver);
    if (pdrv && pdrv->remove)
        pdrv->remove(pdev);
}

int platform_driver_register(struct platform_driver *drv)
{
    if (!drv || !drv->name)
        return -1;
    init_driver(&drv->driver, drv->name, &platform_bus_type, platform_driver_probe);
    drv->driver.remove = platform_driver_remove;
    return driver_register(&drv->driver);
}

int platform_driver_unregister(struct platform_driver *drv)
{
    if (!drv)
        return -1;
    return driver_unregister(&drv->driver);
}

struct platform_device *platform_device_register_simple(const char *name, int id)
{
    if (!name || !*name)
        return NULL;

    struct platform_device *pdev = (struct platform_device *)kmalloc(sizeof(*pdev));
    if (!pdev)
        return NULL;
    memset(pdev, 0, sizeof(*pdev));
    pdev->name = name;
    pdev->id = id;

    char inst[32];
    uint32_t n = (uint32_t)strlen(name);
    if (n >= sizeof(inst))
        n = sizeof(inst) - 1;
    memcpy(inst, name, n);
    inst[n] = 0;
    if (id != PLATFORM_DEVID_NONE) {
        char tmp[12];
        snprintf(tmp, sizeof(tmp), "%d", id);
        uint32_t t = (uint32_t)strlen(tmp);
        if (n + t >= sizeof(inst))
            t = sizeof(inst) - n - 1;
        memcpy(inst + n, tmp, t);
        inst[n + t] = 0;
    }

    device_initialize(&pdev->dev, inst);
    pdev->dev.release = platform_device_release;
    device_set_parent(&pdev->dev, &platform_bus);
    pdev->dev.bus = &platform_bus_type;
    /* Identify platform_device reliably without container_of() guesswork. */
    pdev->dev.driver_data = pdev;

    if (device_add(&pdev->dev) != 0) {
        kobject_put(&pdev->dev.kobj);
        return NULL;
    }
    return pdev;
}
