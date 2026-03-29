#include "linux/device.h"
#include "linux/libc.h"

static struct bus_type platform_bus;
static struct class console_class;
static struct class tty_class;

struct class *device_model_console_class(void)
{
    if (!device_model_inited())
        return NULL;
    return &console_class;
}

struct class *device_model_tty_class(void)
{
    if (!device_model_inited())
        return NULL;
    return &tty_class;
}

struct bus_type *device_model_platform_bus(void)
{
    if (!device_model_inited())
        return NULL;
    return &platform_bus;
}

void driver_init(void)
{
    if (device_model_inited())
        return;
    memset(&platform_bus, 0, sizeof(platform_bus));
    kobject_init(&platform_bus.kobj, "platform", NULL);
    platform_bus.match = bus_default_match;
    platform_bus.devices = NULL;
    platform_bus.drivers = NULL;
    platform_bus.next = NULL;
    memset(&console_class, 0, sizeof(console_class));
    kobject_init(&console_class.kobj, "console", NULL);
    memset(&tty_class, 0, sizeof(tty_class));
    kobject_init(&tty_class.kobj, "tty", NULL);
    device_model_kset_init();
    class_register(&console_class);
    class_register(&tty_class);
    device_model_mark_inited();
    printf("Driver core initialized.\n");
}
