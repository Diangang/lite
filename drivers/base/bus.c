#include "linux/device.h"
#include "linux/slab.h"
#include "linux/libc.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static struct bus_type *bus_list = NULL;

static void bus_release_kobj(struct kobject *kobj)
{
    struct bus_type *bus = CONTAINER_OF(kobj, struct bus_type, kobj);
    kfree(bus);
}

int bus_default_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    if (!dev->type)
        return 0;
    if (!drv->kobj.name[0])
        return 0;
    return strcmp(dev->type, drv->kobj.name) == 0;
}

struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *))
{
    struct bus_type *bus = (struct bus_type*)kmalloc(sizeof(struct bus_type));
    if (!bus)
        return NULL;
    memset(bus, 0, sizeof(*bus));
    kobject_init(&bus->kobj, name, bus_release_kobj);
    bus->match = match ? match : bus_default_match;
    bus->next = bus_list;
    bus_list = bus;
    return bus;
}
