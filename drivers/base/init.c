#include "linux/device.h"
#include "linux/libc.h"

static struct bus_type platform_bus;

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
    device_model_kset_init();
    device_model_mark_inited();
    printf("Driver core initialized.\n");
}
