#include "linux/device.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/errno.h"
#include "base.h"

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

static uint32_t driver_attr_show_name(struct device_driver *drv, struct driver_attribute *attr,
                                      char *buffer, uint32_t cap)
{
    (void)attr;
    if (!drv)
        return 0;
    if (!buffer || cap < 2)
        return 0;
    if (!drv->name)
        return sysfs_emit_text_line(buffer, cap, "unknown");
    return sysfs_emit_text_line(buffer, cap, drv->name);
}

static struct driver_attribute drv_attr_name = {
    .attr = { .name = "name", .mode = 0444 },
    .show = driver_attr_show_name,
};

static const struct attribute *driver_default_attrs[] = {
    &drv_attr_name.attr,
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

static uint32_t driver_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    struct driver_attribute *dattr;

    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    dattr = container_of(attr, struct driver_attribute, attr);
    return dattr->show ? dattr->show(container_of(kobj, struct device_driver, kobj), dattr, buffer, cap) : 0;
}

static uint32_t driver_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    struct driver_attribute *dattr;

    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    dattr = container_of(attr, struct driver_attribute, attr);
    return dattr->store ? dattr->store(container_of(kobj, struct device_driver, kobj),
                                       dattr, offset, size, buffer) : 0;
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

int driver_create_file(struct device_driver *drv, const struct driver_attribute *attr)
{
    if (!drv || !attr)
        return -1;
    return sysfs_create_file(&drv->kobj, &attr->attr);
}

void driver_remove_file(struct device_driver *drv, const struct driver_attribute *attr)
{
    if (!drv || !attr)
        return;
    sysfs_remove_file(&drv->kobj, &attr->attr);
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
    (void)add_bind_files(drv);
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
    remove_bind_files(drv);
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
    kobject_init_with_ktype(&drv->kobj, name, &driver_ktype, NULL);
    klist_init(&drv->klist_devices, NULL, NULL);
    INIT_LIST_HEAD(&drv->knode_bus.n_node);
    drv->knode_bus.n_klist = NULL;
    kref_init(&drv->knode_bus.n_ref);
    drv->bus = bus;
    drv->probe = probe;
}

/* kobj_type is internal; sysfs should not need driver-type checks. */
