#ifndef LINUX_PLATFORM_DEVICE_H
#define LINUX_PLATFORM_DEVICE_H

#include <stdint.h>
#include "linux/device.h"
#include "linux/kernel.h"

/* Linux 2.6 compatible naming (minimal subset) for platform devices/drivers. */
#define PLATFORM_DEVID_NONE (-1)

struct platform_device_id {
    const char *name;
    unsigned long driver_data;
};

struct platform_device {
    const char *name; /* functional name, e.g. "serial" */
    int id;           /* instance id, e.g. 0 => "serial0" */
    struct device dev;
};

struct platform_driver {
    const char *name;
    const struct platform_device_id *id_table;
    int (*probe)(struct platform_device *pdev);
    void (*remove)(struct platform_device *pdev);

    struct device_driver driver;
};

static inline struct platform_driver *to_platform_driver(struct device_driver *drv)
{
    return container_of(drv, struct platform_driver, driver);
}

extern struct bus_type platform_bus_type;

int platform_driver_register(struct platform_driver *drv);
int platform_driver_unregister(struct platform_driver *drv);

struct platform_device *platform_device_register_simple(const char *name, int id);
struct platform_device *platform_get_platform_device(struct device *dev);

/* platform bus match hook (installed by driver core init). */
int platform_bus_match(struct device *dev, struct device_driver *drv);

#endif
