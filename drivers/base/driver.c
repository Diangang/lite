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

static struct device_driver *driver_from_bus_entry(struct list_head *entry)
{
    struct kobject *kobj;

    if (!entry)
        return NULL;
    kobj = container_of(entry, struct kobject, entry);
    return container_of(kobj, struct device_driver, kobj);
}

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
    struct device *cur;
    list_for_each_entry(cur, &drv->bus->devices, bus_list) {
        if (!strcmp(cur->kobj.name, name))
            return cur;
    }
    return NULL;
}

static uint32_t driver_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    struct device_driver *drv = container_of(kobj, struct device_driver, kobj);
    if (!strcmp(attr->name, "name"))
        return sysfs_emit_text_line(buffer, cap, drv ? drv->kobj.name : "unknown");
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
    struct device *dev;
    list_for_each_entry(dev, &drv->bus->devices, bus_list) {
        if (dev->driver)
            continue;
        int rc = driver_probe_device(drv, dev);
        if (rc == -EPROBE_DEFER)
            driver_deferred_probe_add(dev);
    }
    return 0;
}

/* driver_register: Implement driver register. */
int driver_register(struct device_driver *drv)
{
    struct list_head *entry;

    if (!drv || !drv->bus)
        return -1;
    list_for_each(entry, &drv->bus->drivers.list) {
        struct device_driver *cur = driver_from_bus_entry(entry);
        if (!cur)
            continue;
        if (!strcmp(cur->kobj.name, drv->kobj.name))
            return -1;
    }
    drv->kobj.kset = &drv->bus->drivers;
    kset_add(&drv->bus->drivers, &drv->kobj);
    if (drv->bus->drivers_autoprobe)
        driver_attach(drv);
    driver_deferred_probe_trigger();
    return 0;
}

/* driver_unregister: Implement driver unregister. */
int driver_unregister(struct device_driver *drv)
{
    if (!drv || !drv->bus)
        return -1;
    struct device *dev;
    list_for_each_entry(dev, &drv->bus->devices, bus_list) {
        if (dev->driver == drv)
            device_unbind(dev);
    }
    sysfs_remove_dir(&drv->kobj);
    kset_remove(&drv->bus->drivers, &drv->kobj);
    drv->kobj.kset = NULL;
    /* Drop the registration reference (Linux mapping: driver_unregister ends the kobject lifetime). */
    kobject_put(&drv->kobj);
    return 0;
}

/* init_driver: Initialize driver. */
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv)
        return;
    memset(drv, 0, sizeof(*drv));
    kobject_init_with_ktype(&drv->kobj, name, &ktype_driver, NULL);
    INIT_LIST_HEAD(&drv->devices);
    drv->bus = bus;
    drv->probe = probe;
}

struct kobj_type *ktype_driver_get(void)
{
    return &ktype_driver;
}
