#include "linux/device.h"
#include "base.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/sysfs.h"

/* Linux mapping: linux2.6/drivers/base/bus.c uses bus_kset as /sys/bus root. */
struct kset *bus_kset;
static LIST_HEAD(bus_list_head);

#define to_bus_attr(_attr) container_of(_attr, struct bus_attribute, attr)

static struct device *bus_device_from_node(struct klist_node *node)
{
    return node ? container_of(node, struct device, knode_bus) : NULL;
}

static struct device_driver *bus_driver_from_node(struct klist_node *node)
{
    return node ? container_of(node, struct device_driver, knode_bus) : NULL;
}

static void bus_device_klist_get(struct klist_node *node)
{
    struct device *dev = bus_device_from_node(node);
    if (dev)
        kobject_get(&dev->kobj);
}

static void bus_device_klist_put(struct klist_node *node)
{
    struct device *dev = bus_device_from_node(node);
    if (dev)
        kobject_put(&dev->kobj);
}

static void bus_driver_klist_get(struct klist_node *node)
{
    struct device_driver *drv = bus_driver_from_node(node);
    if (drv)
        kobject_get(&drv->kobj);
}

static void bus_driver_klist_put(struct klist_node *node)
{
    struct device_driver *drv = bus_driver_from_node(node);
    if (drv)
        kobject_put(&drv->kobj);
}

static inline struct kobject *bus_kobj(struct bus_type *bus)
{
    return bus ? &bus->subsys.kset.kobj : NULL;
}

static void bus_sysfs_unregister_subdirs(struct bus_type *bus)
{
    if (!bus)
        return;
    kobject_del(bus_drivers_kobj(bus));
    kobject_del(bus_devices_kobj(bus));
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
    bus_sysfs_unregister_subdirs(bus);
    kfree(bus->p);
    bus->p = NULL;
}

static uint32_t bus_attr_show_drivers_autoprobe(struct bus_type *bus, struct bus_attribute *attr,
                                                char *buffer, uint32_t cap)
{
    (void)attr;
    return bus_emit_text_line(buffer, cap, bus_drivers_autoprobe(bus) ? "1" : "0");
}

static uint32_t bus_attr_store_drivers_autoprobe(struct bus_type *bus, struct bus_attribute *attr,
                                                 uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)attr;
    if (!bus || !buffer || offset || size == 0)
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        if (buffer[i] == '0') {
            bus_set_drivers_autoprobe(bus, 0);
            return size;
        }
        if (buffer[i] == '1') {
            bus_set_drivers_autoprobe(bus, 1);
            return size;
        }
    }
    return 0;
}

static int bus_probe_device_name(struct bus_type *bus, const char *name)
{
    struct klist_iter iter;
    struct klist_node *node;
    if (!bus || !name || !name[0])
        return -1;
    klist_iter_init(bus_devices_klist(bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = bus_device_from_node(node);
        if (strcmp(dev->kobj.name, name))
            continue;
        /* Linux-like boundary: drivers_probe does not silently reprobe a bound device. */
        if (dev->driver)
            break;
        klist_iter_exit(&iter);
        return device_attach(dev);
    }
    klist_iter_exit(&iter);
    return -1;
}

static int bus_probe_device_modalias(struct bus_type *bus, const char *modalias)
{
    struct klist_iter iter;
    struct klist_node *node;
    char buf[128];

    if (!bus || !modalias || !modalias[0])
        return -1;
    klist_iter_init(bus_devices_klist(bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = bus_device_from_node(node);
        if (device_get_modalias(dev, buf, sizeof(buf)) != 0)
            continue;
        if (strcmp(buf, modalias))
            continue;
        /* Linux-like boundary: explicit unbind is required before rebinding. */
        if (dev->driver)
            break;
        klist_iter_exit(&iter);
        return device_attach(dev);
    }
    klist_iter_exit(&iter);
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
    struct kset *devices = bus_devices_kset(bus);
    struct kset *drivers = bus_drivers_kset(bus);
    if (!devices || !drivers)
        return -1;
    kset_init(devices, "devices");
    devices->kobj.parent = bus_kobj(bus);
    if (kobject_add(&devices->kobj) != 0)
        return -1;
    kset_init(drivers, "drivers");
    drivers->kobj.parent = bus_kobj(bus);
    if (kobject_add(&drivers->kobj) != 0) {
        kobject_del(&devices->kobj);
        return -1;
    }
    return 0;
}

void buses_init(void)
{
    static struct kset bus_kset_storage;
    bus_kset = &bus_kset_storage;
    kset_init(bus_kset, "bus");
    /* Linux mapping: kset_create_and_add("bus", ...) */
    (void)kobject_add(&bus_kset->kobj);
}

/* bus_default_match: Implement bus default match. */
int bus_default_match(struct device *dev, struct device_driver *drv)
{
    const char *drv_name;

    if (!dev || !drv)
        return 0;
    drv_name = drv->name ? drv->name : drv->kobj.name;
    if (dev->bus == &platform_bus_type) {
        struct platform_device *pdev = platform_get_platform_device(dev);
        if (pdev && pdev->name && drv_name[0] && strcmp(pdev->name, drv_name) == 0)
            return 1;
    }
    if (drv_name[0]) {
        if (strcmp(dev->kobj.name, drv_name) == 0)
            return 1;
        if (dev->type && dev->type->name && strcmp(dev->type->name, drv_name) == 0)
            return 1;
    }
    return 0;
}

int bus_rescan_devices(struct bus_type *bus)
{
    struct klist_iter iter;
    struct klist_node *node;
    if (!bus)
        return -1;
    klist_iter_init(bus_devices_klist(bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = bus_device_from_node(node);
        if (dev->driver)
            continue;
        device_attach(dev);
    }
    klist_iter_exit(&iter);
    return 0;
}

int bus_register(struct bus_type *bus)
{
    int allocated_p = 0;
    if (!bus)
        return -1;
    if (!bus->name || !bus->name[0])
        return -1;
    struct bus_type *cur;
    list_for_each_entry(cur, &bus_list_head, list) {
        if (!strcmp(cur->name, bus->name))
            return -1;
    }
    if (!bus->p) {
        bus->p = (struct subsys_private *)kmalloc(sizeof(*bus->p));
        if (!bus->p)
            return -1;
        memset(bus->p, 0, sizeof(*bus->p));
        bus->p->bus = bus;
        allocated_p = 1;
    }
    if (!bus->match)
        bus->match = bus_default_match;
    bus_set_drivers_autoprobe(bus, 1);
    kset_init(&bus->subsys.kset, bus->name);
    bus->subsys.kset.kobj.ktype = &ktype_bus;
    bus->subsys.kset.kobj.kset = bus_kset;
    klist_init(bus_devices_klist(bus), bus_device_klist_get, bus_device_klist_put);
    klist_init(bus_drivers_klist(bus), bus_driver_klist_get, bus_driver_klist_put);
    if (!bus->list.next || !bus->list.prev)
        INIT_LIST_HEAD(&bus->list);
    list_add_tail(&bus->list, &bus_list_head);
    if (subsystem_register(&bus->subsys) != 0) {
        list_del(&bus->list);
        if (allocated_p) {
            kfree(bus->p);
            bus->p = NULL;
        }
        return -1;
    }
    if (bus_sysfs_register_subdirs(bus) != 0) {
        subsystem_unregister(&bus->subsys);
        list_del(&bus->list);
        if (allocated_p) {
            kfree(bus->p);
            bus->p = NULL;
        }
        return -1;
    }
    return 0;
}

/* Linux alignment: do not provide global bus enumeration helpers. */
