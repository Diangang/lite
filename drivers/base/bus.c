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
    if (drv->id_table) {
        const struct device_id *id = drv->id_table;
        while (id->name || id->type || id->vendor_id || id->device_id || id->class_id || id->subclass_id) {
            int match = 1;
            if (id->name && strcmp(dev->kobj.name, id->name) != 0)
                match = 0;
            if (id->type && dev->type && strcmp(dev->type, id->type) != 0)
                match = 0;
            if (id->type && !dev->type)
                match = 0;
            if (id->vendor_id && id->vendor_id != dev->vendor_id)
                match = 0;
            if (id->device_id && id->device_id != dev->device_id)
                match = 0;
            if (id->class_id && id->class_id != dev->class_id)
                match = 0;
            if (id->subclass_id && id->subclass_id != dev->subclass_id)
                match = 0;
            if (match)
                return 1;
            id++;
        }
    }
    if (drv->kobj.name[0]) {
        if (strcmp(dev->kobj.name, drv->kobj.name) == 0)
            return 1;
        if (dev->type && strcmp(dev->type, drv->kobj.name) == 0)
            return 1;
    }
    return 0;
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
