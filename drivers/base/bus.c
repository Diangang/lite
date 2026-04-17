#include "linux/device.h"
#include "base.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/sysfs.h"

static struct subsystem bus_subsys;
static LIST_HEAD(bus_list_head);

#define to_bus_attr(_attr) container_of(_attr, struct bus_attribute, attr)

static inline struct kobject *bus_kobj(struct bus_type *bus)
{
    return bus ? &bus->subsys.kset.kobj : NULL;
}

static inline struct kobject *bus_drivers_kobj(struct bus_type *bus)
{
    return bus ? &bus->drivers.kobj : NULL;
}

static uint32_t bus_emit_text_line(char *buffer, uint32_t cap, const char *text)
{
    uint32_t n;
    if (!buffer || cap == 0)
        return 0;
    if (!text)
        text = "";
    n = (uint32_t)strlen(text);
    if (n + 1 >= cap)
        return 0;
    memcpy(buffer, text, n);
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

/* bus_release_kobj: Implement bus release kobj. */
static void bus_release_kobj(struct kobject *kobj)
{
    struct bus_type *bus = container_of(kobj, struct bus_type, subsys.kset.kobj);
    sysfs_remove_dir(bus_drivers_kobj(bus));
    sysfs_remove_subdir(bus_kobj(bus), "devices");
    sysfs_remove_dir(bus_kobj(bus));
    kfree(bus);
}

static uint32_t bus_attr_show_drivers_autoprobe(struct bus_type *bus, struct bus_attribute *attr,
                                                char *buffer, uint32_t cap)
{
    (void)attr;
    return bus_emit_text_line(buffer, cap, (bus && bus->drivers_autoprobe) ? "1" : "0");
}

static uint32_t bus_attr_store_drivers_autoprobe(struct bus_type *bus, struct bus_attribute *attr,
                                                 uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)attr;
    if (!bus || !buffer || offset || size == 0)
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        if (buffer[i] == '0') {
            bus->drivers_autoprobe = 0;
            return size;
        }
        if (buffer[i] == '1') {
            bus->drivers_autoprobe = 1;
            return size;
        }
    }
    return 0;
}

static int bus_probe_device_name(struct bus_type *bus, const char *name)
{
    struct device *dev;
    if (!bus || !name || !name[0])
        return -1;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (strcmp(dev->kobj.name, name))
            continue;
        /* Linux-like boundary: drivers_probe does not silently reprobe a bound device. */
        if (dev->driver)
            return -1;
        return device_attach(dev);
    }
    return -1;
}

static int bus_probe_device_modalias(struct bus_type *bus, const char *modalias)
{
    struct device *dev;
    char buf[128];

    if (!bus || !modalias || !modalias[0])
        return -1;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (device_get_modalias(dev, buf, sizeof(buf)) != 0)
            continue;
        if (strcmp(buf, modalias))
            continue;
        /* Linux-like boundary: explicit unbind is required before rebinding. */
        if (dev->driver)
            return -1;
        return device_attach(dev);
    }
    return -1;
}

static uint32_t bus_attr_store_drivers_probe(struct bus_type *bus, struct bus_attribute *attr,
                                             uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)attr;
    char tmp[64];
    uint32_t n;
    if (!bus || !buffer || offset || size == 0)
        return 0;
    n = size;
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, buffer, n);
    tmp[n] = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (tmp[i] == '\n' || tmp[i] == ' ' || tmp[i] == '\t') {
            tmp[i] = 0;
            break;
        }
    }
    if (!tmp[0])
        return 0;
    if (bus_probe_device_name(bus, tmp) == 0)
        return size;
    return (bus_probe_device_modalias(bus, tmp) == 0) ? size : 0;
}

static struct bus_attribute bus_attr_drivers_autoprobe = {
    .attr = { .name = "drivers_autoprobe", .mode = 0644 },
    .show = bus_attr_show_drivers_autoprobe,
    .store = bus_attr_store_drivers_autoprobe,
};

static struct bus_attribute bus_attr_drivers_probe = {
    .attr = { .name = "drivers_probe", .mode = 0200 },
    .show = NULL,
    .store = bus_attr_store_drivers_probe,
};

static const struct attribute *bus_default_attrs[] = {
    &bus_attr_drivers_autoprobe.attr,
    &bus_attr_drivers_probe.attr,
    NULL,
};

static uint32_t bus_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    struct bus_type *bus;
    struct bus_attribute *battr;
    if (!kobj || !attr || !buffer)
        return 0;
    bus = container_of(kobj, struct bus_type, subsys.kset.kobj);
    battr = to_bus_attr(attr);
    return battr->show ? battr->show(bus, battr, buffer, cap) : 0;
}

static uint32_t bus_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset,
                                uint32_t size, const uint8_t *buffer)
{
    struct bus_type *bus;
    struct bus_attribute *battr;
    if (!kobj || !attr)
        return 0;
    bus = container_of(kobj, struct bus_type, subsys.kset.kobj);
    battr = to_bus_attr(attr);
    return battr->store ? battr->store(bus, battr, offset, size, buffer) : 0;
}

static const struct sysfs_ops bus_sysfs_ops = {
    .show = bus_sysfs_show,
    .store = bus_sysfs_store,
};

static struct kobj_type ktype_bus = {
    .release = bus_release_kobj,
    .sysfs_ops = &bus_sysfs_ops,
    .default_attrs = bus_default_attrs,
    .default_groups = NULL,
};

static int bus_sysfs_register_subdirs(struct bus_type *bus)
{
    if (!bus)
        return -1;
    if (sysfs_create_subdir(bus_kobj(bus), "devices", 0555) != 0)
        return -1;
    kset_init(&bus->drivers, "drivers");
    bus->drivers.kobj.parent = bus_kobj(bus);
    if (kobject_add(&bus->drivers.kobj) != 0) {
        sysfs_remove_subdir(bus_kobj(bus), "devices");
        return -1;
    }
    return 0;
}

struct kset *buses_kset_get(void)
{
    return &bus_subsys.kset;
}

void buses_init(void)
{
    kset_init(&bus_subsys.kset, "bus");
    (void)subsystem_register(&bus_subsys);
}

/* bus_default_match: Implement bus default match. */
int bus_default_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    if (dev->bus == &platform_bus_type) {
        struct platform_device *pdev = platform_get_platform_device(dev);
        if (pdev && pdev->name && strcmp(pdev->name, drv->kobj.name) == 0)
            return 1;
    }
    if (drv->kobj.name[0]) {
        if (strcmp(dev->kobj.name, drv->kobj.name) == 0)
            return 1;
        if (dev->type && dev->type->name && strcmp(dev->type->name, drv->kobj.name) == 0)
            return 1;
    }
    return 0;
}

int bus_rescan_devices(struct bus_type *bus)
{
    struct device *dev;
    if (!bus)
        return -1;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->driver)
            continue;
        device_attach(dev);
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
    bus->name = name;
    kset_init(&bus->subsys.kset, name);
    bus->subsys.kset.kobj.ktype = &ktype_bus;
    bus->subsys.kset.kobj.kset = buses_kset_get();
    bus->drivers_autoprobe = 1;
    bus->match = match ? match : bus_default_match;
    INIT_LIST_HEAD(&bus->list);
    INIT_LIST_HEAD(&bus->devices);
    list_add_tail(&bus->list, &bus_list_head);
    if (subsystem_register(&bus->subsys) != 0) {
        list_del(&bus->list);
        kobject_put(bus_kobj(bus));
        return NULL;
    }
    if (bus_sysfs_register_subdirs(bus) != 0) {
        subsystem_unregister(&bus->subsys);
        list_del(&bus->list);
        kobject_put(bus_kobj(bus));
        return NULL;
    }
    return bus;
}

int bus_register_static(struct bus_type *bus)
{
    if (!bus)
        return -1;
    if (!bus->name || !bus->name[0])
        return -1;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list) {
        if (!strcmp(cur->name, bus->name))
            return -1;
    }
    if (!bus->match)
        bus->match = bus_default_match;
    bus->drivers_autoprobe = 1;
    kset_init(&bus->subsys.kset, bus->name);
    bus->subsys.kset.kobj.ktype = &ktype_bus;
    bus->subsys.kset.kobj.kset = buses_kset_get();
    if (!bus->devices.next || !bus->devices.prev)
        INIT_LIST_HEAD(&bus->devices);
    if (!bus->list.next || !bus->list.prev)
        INIT_LIST_HEAD(&bus->list);
    list_add_tail(&bus->list, &bus_list_head);
    if (subsystem_register(&bus->subsys) != 0) {
        list_del(&bus->list);
        return -1;
    }
    if (bus_sysfs_register_subdirs(bus) != 0) {
        subsystem_unregister(&bus->subsys);
        list_del(&bus->list);
        return -1;
    }
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
        if (!strcmp(cur->name, name))
            return cur;
    }
    return NULL;
}
