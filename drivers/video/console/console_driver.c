#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"

static struct class console_class;
static struct device_driver drv_console;
static const struct device_id console_ids[] = {
    { .name = "console", .type = "console" },
    { .name = NULL, .type = NULL }
};

/* console_probe: Implement console probe. */
static int console_probe(struct device *dev)
{
    (void)dev;
    return 0;
}

static int console_class_init(void)
{
    memset(&console_class, 0, sizeof(console_class));
    kobject_init(&console_class.kobj, "console", NULL);
    INIT_LIST_HEAD(&console_class.list);
    INIT_LIST_HEAD(&console_class.devices);
    return class_register(&console_class);
}
core_initcall(console_class_init);

static int console_device_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    struct class *cls = device_model_console_class();
    if (!platform || !cls)
        return -1;
    struct device *parent = device_model_find_device("serial0");
    if (!device_register_simple_class_parent("console", "console", platform, cls, parent, NULL))
        return -1;
    return 0;
}
device_initcall(console_device_init);

/* console_driver_init: Initialize console driver. */
static int console_driver_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    if (!platform)
        return -1;

    init_driver_ids(&drv_console, "console", platform, console_ids, console_probe);
    driver_register(&drv_console);
    return 0;
}
module_init(console_driver_init);
