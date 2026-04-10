#include "linux/device.h"
#include "linux/libc.h"
#include "linux/errno.h"

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

static struct kobj_type driver_ktype = {
    .release = NULL,
    .sysfs_ops = &driver_sysfs_ops,
    .default_attrs = NULL,
    .default_groups = driver_default_groups,
};

int driver_probe_device(struct device_driver *drv, struct device *dev)
{
    if (!drv || !dev || !drv->bus)
        return -1;
    if (dev->driver == drv)
        return 0;
    if (dev->driver)
        device_unbind(dev);
    if (drv->bus->match && !drv->bus->match(dev, drv))
        return -1;
    dev->driver = drv;
    int rc = drv->probe ? drv->probe(dev) : 0;
    if (rc != 0) {
        dev->driver = NULL;
        if (rc == -EPROBE_DEFER)
            return -EPROBE_DEFER;
        return -1;
    }
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
    if (!drv || !drv->bus)
        return -1;
    struct device_driver *cur;
    list_for_each_entry(cur, &drv->bus->drivers, bus_list) {
        if (!strcmp(cur->kobj.name, drv->kobj.name))
            return -1;
    }
    kset_add(device_model_drivers_kset(), &drv->kobj);
    INIT_LIST_HEAD(&drv->bus_list);
    list_add_tail(&drv->bus_list, &drv->bus->drivers);
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
    if (drv->bus_list.next && drv->bus_list.prev)
        list_del(&drv->bus_list);
    kset_remove(device_model_drivers_kset(), &drv->kobj);
    return 0;
}

/* init_driver: Initialize driver. */
void init_driver(struct device_driver *drv, const char *name, struct bus_type *bus, int (*probe)(struct device *))
{
    if (!drv)
        return;
    memset(drv, 0, sizeof(*drv));
    kobject_init_with_ktype(&drv->kobj, name, &driver_ktype, NULL);
    INIT_LIST_HEAD(&drv->bus_list);
    drv->bus = bus;
    drv->probe = probe;
}
