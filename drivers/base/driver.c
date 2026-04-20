#include "linux/device.h"
#include "linux/libc.h"
#include "linux/errno.h"
#include "base.h"

static struct attribute drv_attr_name = { .name = "name", .mode = 0444 };
static struct attribute drv_attr_bind = { .name = "bind", .mode = 0222 };
static struct attribute drv_attr_unbind = { .name = "unbind", .mode = 0222 };

static const struct attribute *driver_default_attrs[] = {
    &drv_attr_name,
    &drv_attr_bind,
    &drv_attr_unbind,
    NULL,
};

static uint32_t driver_attr_is_visible(struct kobject *kobj, const struct attribute *attr)
{
    (void)kobj;
    return attr ? attr->mode : 0;
}

static const struct attribute_group driver_default_group = {
    .name = NULL,
    .attrs = driver_default_attrs,
    .is_visible = driver_attr_is_visible,
};

static const struct attribute_group *driver_default_groups[] = {
    &driver_default_group,
    NULL,
};


static uint32_t sysfs_emit_text_line(char *buffer, uint32_t cap, const char *text)
{
    if (!buffer || cap < 2)
        return 0;
    if (!text)
        text = "";
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= cap)
        n = cap - 2;
    memcpy(buffer, text, n);
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static struct device *sysfs_find_driver_bus_device(struct device_driver *drv, const char *name)
{
    if (!drv || !drv->bus || !name || !name[0])
        return NULL;
    struct klist_iter iter;
    struct klist_node *node;
    klist_iter_init(bus_devices_klist(drv->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *cur = container_of(node, struct device, knode_bus);
        if (!strcmp(cur->kobj.name, name))
            break;
    }
    struct device *ret = node ? container_of(node, struct device, knode_bus) : NULL;
    klist_iter_exit(&iter);
    return ret;
}

static uint32_t driver_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    struct device_driver *drv = container_of(kobj, struct device_driver, kobj);
    if (!strcmp(attr->name, "name"))
        return sysfs_emit_text_line(buffer, cap, (drv && drv->name) ? drv->name : "unknown");
    return 0;
}

static uint32_t driver_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!kobj || !attr || !attr->name || !buffer || offset)
        return 0;
    struct device_driver *drv = container_of(kobj, struct device_driver, kobj);
    if (!drv)
        return 0;

    char tmp[64];
    uint32_t n = size;
    if (n >= sizeof(tmp))
        n = sizeof(tmp) - 1;
    memcpy(tmp, buffer, n);
    tmp[n] = 0;
    for (uint32_t i = 0; i < n; i++)
        if (tmp[i] == '\n' || tmp[i] == ' ')
            tmp[i] = 0;
    if (!tmp[0])
        return 0;

    struct device *dev = sysfs_find_driver_bus_device(drv, tmp);
    if (!dev)
        return 0;

    if (!strcmp(attr->name, "bind")) {
        if (driver_probe_device(drv, dev) != 0)
            return 0;
        return size;
    }
    if (!strcmp(attr->name, "unbind")) {
        if (dev->driver != drv)
            return 0;
        device_release_driver(dev);
        return size;
    }
    return 0;
}

static const struct sysfs_ops driver_sysfs_ops = {
    .show = driver_sysfs_show,
    .store = driver_sysfs_store,
};

static struct kobj_type ktype_driver = {
    .release = NULL,
    .sysfs_ops = &driver_sysfs_ops,
    .default_attrs = NULL,
    .default_groups = driver_default_groups,
};

static struct device *deferred_devs[64];
static uint32_t deferred_devs_count = 0;

static void driver_deferred_probe_remove_index(uint32_t idx);

void driver_deferred_probe_add(struct device *dev)
{
    if (!dev)
        return;
    for (uint32_t i = 0; i < deferred_devs_count; i++) {
        if (deferred_devs[i] == dev)
            return;
    }
    if (deferred_devs_count < (uint32_t)(sizeof(deferred_devs) / sizeof(deferred_devs[0])))
        deferred_devs[deferred_devs_count++] = dev;
}

void driver_deferred_probe_remove(struct device *dev)
{
    if (!dev)
        return;
    uint32_t i = 0;
    while (i < deferred_devs_count) {
        if (deferred_devs[i] == dev) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        i++;
    }
}

static void driver_deferred_probe_remove_index(uint32_t idx)
{
    if (idx >= deferred_devs_count)
        return;
    for (uint32_t j = idx + 1; j < deferred_devs_count; j++)
        deferred_devs[j - 1] = deferred_devs[j];
    deferred_devs_count--;
}

void driver_deferred_probe_trigger(void)
{
    uint32_t i = 0;
    while (i < deferred_devs_count) {
        struct device *dev = deferred_devs[i];
        if (!dev || dev->driver) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        device_attach(dev);
        if (dev->driver) {
            driver_deferred_probe_remove_index(i);
            continue;
        }
        i++;
    }
}

int driver_probe_device(struct device_driver *drv, struct device *dev)
{
    if (!drv || !dev || !drv->bus)
        return -1;
    if (dev->driver == drv)
        return 0;
    /*
     * Linux mapping: explicit bind should not silently steal a device from an
     * already bound driver. Reprobe paths must detach first via
     * device_release_driver()/device_reprobe().
     */
    if (dev->driver)
        return -1;
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return -1;

    /* Hold a driver reference for the duration of the device->driver binding. */
    kobject_get(&drv->kobj);
    dev->driver = drv;
    int rc = drv->probe ? drv->probe(dev) : 0;
    if (rc != 0) {
        sysfs_remove_link(&dev->kobj, "driver");
        dev->driver = NULL;
        kobject_put(&drv->kobj);
        if (rc == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        return -1;
    }
    sysfs_create_link(&drv->kobj, &dev->kobj, dev->kobj.name);
    sysfs_create_link(&dev->kobj, &drv->kobj, "driver");
    driver_deferred_probe_remove(dev);
    device_uevent_emit("bind", dev);
    return 0;
}

/* driver_bind_device: Implement driver bind device. */
int driver_bind_device(struct device_driver *drv, struct device *dev)
{
    return driver_probe_device(drv, dev);
}

int driver_attach(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    struct klist_iter iter;
    struct klist_node *node;
    klist_iter_init(bus_devices_klist(drv->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = container_of(node, struct device, knode_bus);
        if (dev->driver)
            continue;
        int rc = driver_probe_device(drv, dev);
        if (rc == -EPROBE_DEFER)
            driver_deferred_probe_add(dev);
    }
    klist_iter_exit(&iter);
    return 0;
}

/* driver_register: Implement driver register. */
int driver_register(struct device_driver *drv)
{
    struct klist_iter iter;
    struct klist_node *node;

    if (!drv || !drv->bus || !drv->name || !drv->name[0])
        return -1;
    klist_iter_init(bus_drivers_klist(drv->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device_driver *cur = container_of(node, struct device_driver, knode_bus);
        if (cur && cur->name && !strcmp(cur->name, drv->name)) {
            klist_iter_exit(&iter);
            return -1;
        }
    }
    klist_iter_exit(&iter);
    drv->kobj.kset = bus_drivers_kset(drv->bus);
    kset_add(bus_drivers_kset(drv->bus), &drv->kobj);
    klist_add_tail(&drv->knode_bus, bus_drivers_klist(drv->bus));
    if (bus_drivers_autoprobe(drv->bus))
        driver_attach(drv);
    driver_deferred_probe_trigger();
    return 0;
}

/* driver_unregister: Implement driver unregister. */
void driver_unregister(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return;
    struct klist_iter iter;
    struct klist_node *node;
    klist_iter_init(bus_devices_klist(drv->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device *dev = container_of(node, struct device, knode_bus);
        if (dev->driver == drv)
            device_unbind(dev);
    }
    klist_iter_exit(&iter);
    if (klist_node_attached(&drv->knode_bus))
        klist_remove(&drv->knode_bus);
    kobject_del(&drv->kobj);
    kset_remove(bus_drivers_kset(drv->bus), &drv->kobj);
    drv->kobj.kset = NULL;
    /* Drop the registration reference (Linux mapping: driver_unregister ends the kobject lifetime). */
    kobject_put(&drv->kobj);
}

/* init_driver: Initialize driver. */
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv)
        return;
    memset(drv, 0, sizeof(*drv));
    drv->name = name;
    kobject_init_with_ktype(&drv->kobj, name, &ktype_driver, NULL);
    klist_init(&drv->klist_devices, NULL, NULL);
    INIT_LIST_HEAD(&drv->knode_bus.n_node);
    drv->knode_bus.n_klist = NULL;
    kref_init(&drv->knode_bus.n_ref);
    drv->bus = bus;
    drv->probe = probe;
}

struct kobj_type *ktype_driver_get(void)
{
    return &ktype_driver;
}
