#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/devtmpfs.h"
#include "linux/sysfs.h"
#include "linux/errno.h"
#include "linux/platform_device.h"
#include "base.h"

static struct subsystem devices_subsys;

struct kset *devices_kset_get(void)
{
    return &devices_subsys.kset;
}

/* device_release_kobj: Implement device release kobj. */
static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = container_of(kobj, struct device, kobj);
    sysfs_remove_dir(&dev->kobj);
    if (dev->release)
        dev->release(dev);
    else
        kfree(dev);
}

static struct kobj_type ktype_device = {
    .release = device_release_kobj,
    .sysfs_ops = NULL,
    .default_attrs = NULL,
    .default_groups = NULL,
};

struct kobj_type *ktype_device_get(void)
{
    return &ktype_device;
}

static void device_default_release(struct device *dev)
{
    kfree(dev);
}

void device_initialize(struct device *dev, const char *name)
{
    if (!dev)
        return;
    kobject_init_with_ktype(&dev->kobj, name, &ktype_device, NULL);
    INIT_LIST_HEAD(&dev->bus_list);
    INIT_LIST_HEAD(&dev->class_list);
    dev->groups = NULL;
    dev->devt = 0;
    if (!dev->release)
        dev->release = device_default_release;
}

const char *device_get_devnode(struct device *dev)
{
    if (!dev || !dev->type || !dev->type->devnode)
        return NULL;
    return dev->type->devnode(dev);
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

static uint32_t sysfs_emit_devno_line(char *buffer, uint32_t cap, dev_t devt)
{
    if (!buffer || cap < 4)
        return 0;
    itoa((int)MAJOR(devt), 10, buffer);
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return 0;
    buffer[n++] = ':';
    itoa((int)MINOR(devt), 10, buffer + n);
    n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return 0;
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static uint32_t device_attr_show_type(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    return sysfs_emit_text_line(buffer, cap, (dev && dev->type && dev->type->name) ? dev->type->name : "unknown");
}

static uint32_t device_attr_show_dev(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    return sysfs_emit_devno_line(buffer, cap, dev ? dev->devt : 0);
}

static struct device_attribute dev_attr_type = {
    .attr = { .name = "type", .mode = 0444 },
    .show = device_attr_show_type,
};

static struct device_attribute dev_attr_dev = {
    .attr = { .name = "dev", .mode = 0444 },
    .show = device_attr_show_dev,
};

static const struct attribute *device_default_attrs[] = {
    &dev_attr_type.attr,
    &dev_attr_dev.attr,
    NULL,
};

static uint32_t device_attr_is_visible(struct kobject *kobj, const struct attribute *attr)
{
    if (!kobj || !attr || !attr->name)
        return 0;
    struct device *dev = container_of(kobj, struct device, kobj);
    if (!strcmp(attr->name, "type"))
        return (dev && dev->type && dev->type->name) ? attr->mode : 0;
    if (!strcmp(attr->name, "dev"))
        return (dev && (device_get_devnode(dev) || dev->devt)) ? attr->mode : 0;

    return attr->mode;
}

static const struct attribute_group device_default_group = {
    .name = NULL,
    .attrs = device_default_attrs,
    .is_visible = device_attr_is_visible,
};

static const struct attribute_group *device_default_groups[] = {
    &device_default_group,
    NULL,
};

static uint32_t device_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    struct device *dev = container_of(kobj, struct device, kobj);
    struct device_attribute *dattr = container_of(attr, struct device_attribute, attr);
    return dattr->show ? dattr->show(dev, dattr, buffer, cap) : 0;
}

static uint32_t device_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    struct device *dev = container_of(kobj, struct device, kobj);
    struct device_attribute *dattr = container_of(attr, struct device_attribute, attr);
    return dattr->store ? dattr->store(dev, dattr, offset, size, buffer) : 0;
}

static const struct sysfs_ops dev_sysfs_ops = {
    .show = device_sysfs_show,
    .store = device_sysfs_store,
};

static void device_sysfs_init_ktype(void)
{
    ktype_device.sysfs_ops = &dev_sysfs_ops;
    ktype_device.default_groups = device_default_groups;
}

/* kobject_child_add: Implement kobject child add. */
static void kobject_child_add(struct kobject *parent, struct kobject *child)
{
    if (!parent || !child)
        return;
    child->next = parent->children;
    parent->children = child;
}

/* kobject_child_del: Implement kobject child del. */
static void kobject_child_del(struct kobject *parent, struct kobject *child)
{
    if (!parent || !child)
        return;
    if (parent->children == child) {
        parent->children = child->next;
        child->next = NULL;
        return;
    }
    struct kobject *prev = parent->children;
    while (prev && prev->next) {
        if (prev->next == child) {
            prev->next = child->next;
            child->next = NULL;
            return;
        }
        prev = prev->next;
    }
}

static struct kobject *device_kobj_parent(struct device *dev)
{
    if (!dev)
        return NULL;
    if (dev->kobj.parent)
        return dev->kobj.parent;
    if (dev->kobj.kset && &dev->kobj.kset->kobj != &dev->kobj)
        return &dev->kobj.kset->kobj;
    return NULL;
}

static int device_is_bus_view_hidden(struct device *dev)
{
    return dev && dev == pci_root_device();
}

static void device_sysfs_add_links(struct device *dev)
{
    struct kobject *parent;

    if (!dev)
        return;
    parent = device_kobj_parent(dev);
    if (parent)
        (void)sysfs_create_link(&dev->kobj, parent, "parent");
    if (dev->class) {
        (void)sysfs_create_link(&dev->kobj, &dev->class->subsys.kset.kobj, "subsystem");
        (void)sysfs_create_link(&dev->class->subsys.kset.kobj, &dev->kobj, dev->kobj.name);
    }
    if (dev->bus) {
        if (!dev->class)
            (void)sysfs_create_link(&dev->kobj, &dev->bus->subsys.kset.kobj, "subsystem");
        if (!device_is_bus_view_hidden(dev))
            (void)sysfs_create_link(&dev->bus->subsys.kset.kobj, &dev->kobj, dev->kobj.name);
    }
}

static void device_sysfs_remove_links(struct device *dev)
{
    struct kobject *parent;

    if (!dev)
        return;
    parent = device_kobj_parent(dev);
    if (parent)
        sysfs_remove_link(&dev->kobj, "parent");
    if (dev->class) {
        sysfs_remove_link(&dev->kobj, "subsystem");
        sysfs_remove_link(&dev->class->subsys.kset.kobj, dev->kobj.name);
    }
    if (dev->bus) {
        if (!dev->class)
            sysfs_remove_link(&dev->kobj, "subsystem");
        if (!device_is_bus_view_hidden(dev))
            sysfs_remove_link(&dev->bus->subsys.kset.kobj, dev->kobj.name);
    }
}

/* device_set_parent: Implement device set parent. */
int device_set_parent(struct device *dev, struct device *parent)
{
    if (!dev)
        return -1;

    struct kobject *old_parent = device_kobj_parent(dev);
    if (old_parent)
        kobject_child_del(old_parent, &dev->kobj);
    if (dev->kobj.sd && old_parent)
        sysfs_remove_link(&dev->kobj, "parent");
    dev->kobj.parent = parent ? &parent->kobj : NULL;
    if (dev->kobj.parent)
        kobject_child_add(dev->kobj.parent, &dev->kobj);
    if (dev->kobj.sd && dev->kobj.parent)
        (void)sysfs_create_link(&dev->kobj, dev->kobj.parent, "parent");
    return 0;
}

/* device_unbind: Implement device unbind. */
int device_unbind(struct device *dev)
{
    if (!dev)
        return -1;
    if (!dev->driver)
        return 0;
    struct device_driver *drv = dev->driver;
    dev->driver = NULL;
    sysfs_remove_link(&dev->kobj, "driver");
    sysfs_remove_link(&drv->kobj, dev->kobj.name);
    if (drv->remove)
        drv->remove(dev);
    device_uevent_emit("unbind", dev);
    return 0;
}

int device_release_driver(struct device *dev)
{
    return device_unbind(dev);
}

int device_attach(struct device *dev)
{
    struct list_head *entry;

    if (!dev || !dev->bus)
        return -1;
    if (dev->driver)
        return 0;
    list_for_each(entry, &dev->bus->drivers.list) {
        struct kobject *kobj = container_of(entry, struct kobject, entry);
        struct device_driver *drv = container_of(kobj, struct device_driver, kobj);
        if (dev->bus->match && dev->bus->match(dev, drv)) {
            int rc = driver_probe_device(drv, dev);
            if (rc == -EPROBE_DEFER) {
                driver_deferred_probe_add(dev);
                return 0;
            }
            if (rc == 0)
                return 0;
        }
    }
    return 0;
}

int device_reprobe(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_release_driver(dev);
    return device_attach(dev);
}

/* device_rebind: Implement device rebind. */
int device_rebind(struct device *dev)
{
    return device_reprobe(dev);
}

int device_add(struct device *dev)
{
    /*
     * Linux allows devices without an associated bus (bus == NULL),
     * e.g. many class devices. Keep this for learning parity.
     */
    if (!dev)
        return -1;
    kset_add(devices_kset_get(), &dev->kobj);
    INIT_LIST_HEAD(&dev->bus_list);
    INIT_LIST_HEAD(&dev->class_list);
    if (dev->bus)
        list_add_tail(&dev->bus_list, &dev->bus->devices);
    if (dev->class)
        list_add_tail(&dev->class_list, &dev->class->devices);
    device_sysfs_add_links(dev);
    devtmpfs_register_device(dev);
    device_uevent_emit("add", dev);
    if (dev->bus && dev->bus->drivers_autoprobe)
        device_attach(dev);
    return 0;
}

/* device_register: Implement device register. */
int device_register(struct device *dev)
{
    return device_add(dev);
}

int device_del(struct device *dev)
{
    return device_unregister(dev);
}

/* device_unregister: Implement device unregister. */
int device_unregister(struct device *dev)
{
    if (!dev)
        return -1;
    device_unbind(dev);
    if (dev->bus && dev->bus_list.next && dev->bus_list.prev)
        list_del(&dev->bus_list);
    if (dev->class && dev->class_list.next && dev->class_list.prev)
        list_del(&dev->class_list);
    device_sysfs_remove_links(dev);
    struct kobject *parent = device_kobj_parent(dev);
    if (parent)
        kobject_child_del(parent, &dev->kobj);
    devtmpfs_unregister_device(dev);
    device_uevent_emit("remove", dev);
    kset_remove(&devices_subsys.kset, &dev->kobj);
    kobject_put(&dev->kobj);
    return 0;
}

int device_for_each_child(struct device *dev, void *data, int (*fn)(struct device *child, void *data))
{
    if (!dev || !fn)
        return -1;
    struct kobject *kobj = dev->kobj.children;
    while (kobj) {
        struct device *child = container_of(kobj, struct device, kobj);
        int ret = fn(child, data);
        if (ret)
            return ret;
        kobj = kobj->next;
    }
    return 0;
}

/* device_model_device_count: Implement device model device count. */
uint32_t registered_device_count(void)
{
    uint32_t n = 0;
    struct kset *kset = devices_kset_get();
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        n++;
    return n;
}

/* device_model_device_at: Implement device model device at. */
struct device *registered_device_at(uint32_t index)
{
    struct kset *kset = devices_kset_get();
    uint32_t i = 0;
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry) {
        if (i == index)
            return container_of(kobj, struct device, kobj);
        i++;
    }
    return NULL;
}

/* device_model_find_device: Implement device model find device. */
struct device *find_device_by_name(const char *name)
{
    if (!name)
        return NULL;
    struct kset *kset = devices_kset_get();
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        if (!strcmp(kobj->name, name))
            return container_of(kobj, struct device, kobj);
    return NULL;
}

/* devices_init: Initialize driver-core device anchors. */
void devices_init(void)
{
    device_sysfs_init_ktype();
    kset_init(&devices_subsys.kset, "devices");
    devices_subsys.kset.kobj.ktype = &ktype_device;
    (void)subsystem_register(&devices_subsys);
}
