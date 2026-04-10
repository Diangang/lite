#include "linux/device.h"
#include "linux/libc.h"
#include "linux/platform_device.h"

static struct bus_type platform_bus;

struct class *device_model_console_class(void)
{
    if (!device_model_inited())
        return NULL;
    return class_find("console");
}

/* device_model_tty_class: Implement device model TTY class. */
struct class *device_model_tty_class(void)
{
    if (!device_model_inited())
        return NULL;
    return class_find("tty");
}

/* device_model_block_class: Implement device model block class. */
struct class *device_model_block_class(void)
{
    if (!device_model_inited())
        return NULL;
    return class_find("block");
}

/* device_model_platform_bus: Implement device model platform bus. */
struct bus_type *device_model_platform_bus(void)
{
    if (!device_model_inited())
        return NULL;
    return &platform_bus;
}

/* driver_init: Initialize driver. */
void driver_init(void)
{
    if (device_model_inited())
        return;
    memset(&platform_bus, 0, sizeof(platform_bus));
    kobject_init(&platform_bus.kobj, "platform", NULL);
    platform_bus.match = platform_bus_match;
    INIT_LIST_HEAD(&platform_bus.list);
    INIT_LIST_HEAD(&platform_bus.devices);
    INIT_LIST_HEAD(&platform_bus.drivers);
    bus_register_static(&platform_bus);
    device_model_kset_init();
    device_model_mark_inited();
    struct device *root = device_register_simple("platform", "platform-root", &platform_bus, NULL);
    if (root)
        device_model_set_platform_root(root);

    /* Linux-like sysfs anchor: /sys/devices/virtual */
    struct device *vroot = device_register_simple_class_parent("virtual", "virtual", NULL, NULL, NULL, NULL);
    if (vroot) {
        device_model_set_virtual_root(vroot);
        /* Minimal common virtual subsystems. */
        device_model_virtual_subsys("block");
        device_model_virtual_subsys("tty");
    }
    printf("Driver core initialized.\n");
}
