#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/sysfs.h"
#include "linux/errno.h"
#include "linux/platform_device.h"
#include "linux/vsprintf.h"
#include "base.h"

/* Linux mapping: linux2.6/drivers/base/core.c uses devices_kset as /sys/devices root. */
struct kset *devices_kset;

/* device_release_kobj: Implement device release kobj. */
static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = container_of(kobj, struct device, kobj);
    if (dev->release)
        dev->release(dev);
    else
        kfree(dev);
}

static struct kobj_type device_ktype = {
    .release = device_release_kobj,
    .sysfs_ops = NULL,
    .default_attrs = NULL,
    .default_groups = NULL,
};

static void device_default_release(struct device *dev)
{
    kfree(dev);
}

void device_initialize(struct device *dev, const char *name)
{
    if (!dev)
        return;
    kobject_init_with_ktype(&dev->kobj, name, &device_ktype, NULL);
    INIT_LIST_HEAD(&dev->knode_bus.n_node);
    dev->knode_bus.n_klist = NULL;
    kref_init(&dev->knode_bus.n_ref);
    INIT_LIST_HEAD(&dev->class_list);
    /* Linux mapping: device_initialize should leave the device in a known state. */
    dev->bus = NULL;
    dev->driver = NULL;
    dev->class = NULL;
    dev->groups = NULL;
    dev->type = NULL;
    dev->devt = 0;
    dev->driver_data = NULL;
    if (!dev->release)
        dev->release = device_default_release;
}

const char *device_get_devnode(struct device *dev, uint32_t *mode, uint32_t *uid, uint32_t *gid)
{
    if (!dev)
        return NULL;
    if (dev->type && dev->type->devnode)
        return dev->type->devnode(dev, mode, uid, gid);
    /* Linux mapping: for class devices, class->devnode provides the policy. */
    if (dev->class && dev->class->devnode)
        return dev->class->devnode(dev, mode, uid, gid);
    return NULL;
}

struct device *device_create(struct class *cls, struct device *parent, dev_t devt, void *drvdata, const char *fmt, ...)
{
    if (!cls || !fmt || !fmt[0])
        return NULL;

    char name[64];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    (void)vsnprintf(name, sizeof(name), fmt, args);
    __builtin_va_end(args);
    name[sizeof(name) - 1] = 0;

    struct device *dev = (struct device *)kmalloc(sizeof(*dev));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->class = cls;
    dev->devt = devt;
    dev->driver_data = drvdata;
    if (parent)
        device_set_parent(dev, parent);
    if (device_add(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
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
    snprintf(buffer, cap, "%u:%u", MAJOR(devt), MINOR(devt));
    uint32_t n = (uint32_t)strlen(buffer);
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

static uint32_t device_attr_show_modalias(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    if (!dev || !buffer || cap == 0)
        return 0;
    buffer[0] = 0;
    if (device_get_modalias(dev, buffer, cap) != 0)
        return 0;
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return n;
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static struct device_attribute dev_attr_type = {
    .attr = { .name = "type", .mode = 0444 },
    .show = device_attr_show_type,
};

static struct device_attribute dev_attr_dev = {
    .attr = { .name = "dev", .mode = 0444 },
    .show = device_attr_show_dev,
};

static struct device_attribute dev_attr_modalias = {
    .attr = { .name = "modalias", .mode = 0444 },
    .show = device_attr_show_modalias,
};

static const struct attribute *device_default_attrs[] = {
    &dev_attr_type.attr,
    &dev_attr_dev.attr,
    &dev_attr_modalias.attr,
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
        return (dev && (device_get_devnode(dev, NULL, NULL, NULL) || dev->devt)) ? attr->mode : 0;
    if (!strcmp(attr->name, "modalias")) {
        char modalias[64];
        return (dev && device_get_modalias(dev, modalias, sizeof(modalias)) == 0) ? attr->mode : 0;
    }

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
    device_ktype.sysfs_ops = &dev_sysfs_ops;
    device_ktype.default_groups = device_default_groups;
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
    driver_deferred_probe_remove(dev);
    if (!dev->driver)
        return 0;
    struct device_driver *drv = dev->driver;
    dev->driver = NULL;
    sysfs_remove_link(&dev->kobj, "driver");
    sysfs_remove_link(&drv->kobj, dev->kobj.name);
    if (drv->remove)
        drv->remove(dev);
    device_uevent_emit("unbind", dev);
    kobject_put(&drv->kobj);
    return 0;
}

int device_release_driver(struct device *dev)
{
    return device_unbind(dev);
}

int device_attach(struct device *dev)
{
    struct klist_iter iter;
    struct klist_node *node;

    if (!dev || !dev->bus)
        return -1;
    if (dev->driver)
        return 0;
    klist_iter_init(bus_drivers_klist(dev->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device_driver *drv = container_of(node, struct device_driver, knode_bus);
        if (dev->bus->match && dev->bus->match(dev, drv)) {
            int rc = driver_probe_device(drv, dev);
            if (rc == -EPROBE_DEFER) {
                klist_iter_exit(&iter);
                driver_deferred_probe_add(dev);
                return 0;
            }
            if (rc == 0) {
                klist_iter_exit(&iter);
                return 0;
            }
        }
    }
    klist_iter_exit(&iter);
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
    kset_add(devices_kset, &dev->kobj);
    INIT_LIST_HEAD(&dev->knode_bus.n_node);
    dev->knode_bus.n_klist = NULL;
    kref_init(&dev->knode_bus.n_ref);
    INIT_LIST_HEAD(&dev->class_list);
    if (dev->bus)
        klist_add_tail(&dev->knode_bus, bus_devices_klist(dev->bus));
    if (dev->class)
        list_add_tail(&dev->class_list, &dev->class->devices);
    device_sysfs_add_links(dev);
    devtmpfs_create_node(dev);
    device_uevent_emit("add", dev);
    if (dev->bus && bus_drivers_autoprobe(dev->bus))
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
    device_unregister(dev);
    return 0;
}

/* device_unregister: Implement device unregister. */
void device_unregister(struct device *dev)
{
    if (!dev)
        return;
    /* Avoid deferred-probe UAF if the device is destroyed while deferred. */
    driver_deferred_probe_remove(dev);
    device_unbind(dev);
    if (dev->bus && klist_node_attached(&dev->knode_bus))
        klist_remove(&dev->knode_bus);
    if (dev->class && dev->class_list.next && dev->class_list.prev)
        list_del(&dev->class_list);
    device_sysfs_remove_links(dev);
    devtmpfs_delete_node(dev);
    device_uevent_emit("remove", dev);
    kobject_del(&dev->kobj);
    kset_remove(devices_kset, &dev->kobj);
    kobject_put(&dev->kobj);
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

/* devices_init: Initialize driver-core device anchors. */
void devices_init(void)
{
    static struct kset devices_kset_storage;
    device_sysfs_init_ktype();
    devices_kset = &devices_kset_storage;
    kset_init(devices_kset, "devices");
    (void)kobject_add(&devices_kset->kobj);
}
