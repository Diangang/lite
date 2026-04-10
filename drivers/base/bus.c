#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/sysfs.h"

static LIST_HEAD(bus_list_head);
static struct kobj_type bus_ktype;

/* bus_release_kobj: Implement bus release kobj. */
static void bus_release_kobj(struct kobject *kobj)
{
    struct bus_type *bus = container_of(kobj, struct bus_type, kobj);
    sysfs_remove_dir(&bus->kobj);
    kfree(bus);
}

static struct kobj_type bus_ktype = {
    .release = bus_release_kobj,
    .sysfs_ops = NULL,
    .default_attrs = NULL,
    .default_groups = NULL,
};

/* bus_default_match: Implement bus default match. */
int bus_default_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    if (drv->kobj.name[0]) {
        if (strcmp(dev->kobj.name, drv->kobj.name) == 0)
            return 1;
        if (dev->type && strcmp(dev->type, drv->kobj.name) == 0)
            return 1;
    }
    return 0;
}

/* bus_register: Implement bus register. */
struct bus_type *bus_register(const char *name, int (*match)(struct device *, struct device_driver *))
{
    struct bus_type *bus = (struct bus_type*)kmalloc(sizeof(struct bus_type));
    if (!bus)
        return NULL;
    memset(bus, 0, sizeof(*bus));
    kobject_init_with_ktype(&bus->kobj, name, &bus_ktype, NULL);
    bus->match = match ? match : bus_default_match;
    INIT_LIST_HEAD(&bus->list);
    INIT_LIST_HEAD(&bus->devices);
    INIT_LIST_HEAD(&bus->drivers);
    list_add_tail(&bus->list, &bus_list_head);
    kset_add(device_model_buses_kset(), &bus->kobj);
    return bus;
}

int bus_register_static(struct bus_type *bus)
{
    if (!bus)
        return -1;
    if (!bus->kobj.name[0])
        return -1;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list) {
        if (!strcmp(cur->kobj.name, bus->kobj.name))
            return -1;
    }
    if (!bus->match)
        bus->match = bus_default_match;
    if (!bus->devices.next || !bus->devices.prev)
        INIT_LIST_HEAD(&bus->devices);
    if (!bus->drivers.next || !bus->drivers.prev)
        INIT_LIST_HEAD(&bus->drivers);
    if (!bus->list.next || !bus->list.prev)
        INIT_LIST_HEAD(&bus->list);
    list_add_tail(&bus->list, &bus_list_head);
    kset_add(device_model_buses_kset(), &bus->kobj);
    return 0;
}

uint32_t bus_count(void)
{
    uint32_t n = 0;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list)
        n++;
    return n;
}

struct bus_type *bus_at(uint32_t index)
{
    uint32_t i = 0;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list) {
        if (i == index)
            return cur;
        i++;
    }
    return NULL;
}

struct bus_type *bus_find(const char *name)
{
    if (!name)
        return NULL;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list) {
        if (!strcmp(cur->kobj.name, name))
            return cur;
    }
    return NULL;
}
