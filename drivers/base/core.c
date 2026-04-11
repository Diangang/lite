#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/devtmpfs.h"
#include "linux/sysfs.h"
#include "linux/errno.h"
#include "linux/platform_device.h"
#include "base.h"

static struct kset devices_kset;
static struct kset drivers_kset;
static struct kset classes_kset;
static struct kset buses_kset;
static struct device *platform_root_dev;
static struct device *virtual_root_dev;
static struct device *virtual_subsys_devs[16];
static uint32_t virtual_subsys_count;

struct device *device_model_platform_root(void)
{
    return platform_root_dev;
}

void device_model_set_platform_root(struct device *dev)
{
    platform_root_dev = dev;
}

struct device *device_model_virtual_root(void)
{
    return virtual_root_dev;
}

static void device_model_set_virtual_root(struct device *dev)
{
    virtual_root_dev = dev;
}

static struct device *device_model_find_virtual_subsys(const char *name)
{
    if (!name || !*name)
        return NULL;
    for (uint32_t i = 0; i < virtual_subsys_count; i++) {
        struct device *d = virtual_subsys_devs[i];
        if (d && !strcmp(d->kobj.name, name))
            return d;
    }
    return NULL;
}

struct device *device_model_virtual_subsys(const char *name)
{
    struct device *vr = device_model_virtual_root();
    if (!vr)
        return NULL;
    struct device *d = device_model_find_virtual_subsys(name);
    if (d)
        return d;
    d = device_register_simple_parent(name, "virtual", NULL, vr, NULL);
    if (!d)
        return NULL;
    if (virtual_subsys_count < (uint32_t)(sizeof(virtual_subsys_devs) / sizeof(virtual_subsys_devs[0])))
        virtual_subsys_devs[virtual_subsys_count++] = d;
    return d;
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
    INIT_LIST_HEAD(&dev->bus_list);
    INIT_LIST_HEAD(&dev->class_list);
    dev->groups = NULL;
    if (!dev->release)
        dev->release = device_default_release;
}

static uint32_t device_attr_show_type(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);
static uint32_t device_attr_show_bus(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);
static uint32_t device_attr_show_driver(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);
static uint32_t device_attr_show_modalias(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);
static uint32_t device_attr_show_dev(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap);

static struct device_attribute dev_attr_type = {
    .attr = { .name = "type", .mode = 0444 },
    .show = device_attr_show_type,
};
static struct device_attribute dev_attr_bus = {
    .attr = { .name = "bus", .mode = 0444 },
    .show = device_attr_show_bus,
};
static struct device_attribute dev_attr_driver = {
    .attr = { .name = "driver", .mode = 0444 },
    .show = device_attr_show_driver,
};
static struct device_attribute dev_attr_modalias = {
    .attr = { .name = "modalias", .mode = 0444 },
    .show = device_attr_show_modalias,
};
static struct device_attribute dev_attr_dev = {
    .attr = { .name = "dev", .mode = 0444 },
    .show = device_attr_show_dev,
};

static const struct attribute *device_default_attrs[] = {
    &dev_attr_type.attr,
    &dev_attr_bus.attr,
    &dev_attr_driver.attr,
    &dev_attr_modalias.attr,
    &dev_attr_dev.attr,
    NULL,
};

static uint32_t device_attr_is_visible(struct kobject *kobj, const struct attribute *attr)
{
    if (!kobj || !attr || !attr->name)
        return 0;
    struct device *dev = container_of(kobj, struct device, kobj);
    if (!strcmp(attr->name, "dev"))
        return (dev && (dev->devnode_name || dev->dev_major || dev->dev_minor)) ? attr->mode : 0;

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

static uint32_t sysfs_emit_devno_line(char *buffer, uint32_t cap, uint32_t major, uint32_t minor)
{
    if (!buffer || cap < 4)
        return 0;
    itoa((int)major, 10, buffer);
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return 0;
    buffer[n++] = ':';
    itoa((int)minor, 10, buffer + n);
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
    return sysfs_emit_text_line(buffer, cap, (dev && dev->type) ? dev->type : "unknown");
}

static uint32_t device_attr_show_bus(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    return sysfs_emit_text_line(buffer, cap, (dev && dev->bus) ? dev->bus->kobj.name : "none");
}

static uint32_t device_attr_show_driver(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    return sysfs_emit_text_line(buffer, cap, (dev && dev->driver) ? dev->driver->kobj.name : "unbound");
}

static uint32_t device_attr_show_modalias(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    if (!buffer || cap == 0)
        return 0;
    buffer[0] = 0;
    if (dev)
        device_get_modalias(dev, buffer, cap);
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return n;
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static uint32_t device_attr_show_dev(struct device *dev, struct device_attribute *attr, char *buffer, uint32_t cap)
{
    (void)attr;
    return sysfs_emit_devno_line(buffer, cap, dev ? dev->dev_major : 0, dev ? dev->dev_minor : 0);
}

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

static const struct sysfs_ops device_sysfs_ops = {
    .show = device_sysfs_show,
    .store = device_sysfs_store,
};

static void device_sysfs_init_ktype(void)
{
    device_ktype.sysfs_ops = &device_sysfs_ops;
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

/* device_set_parent: Implement device set parent. */
int device_set_parent(struct device *dev, struct device *parent)
{
    if (!dev)
        return -1;

    if (dev->kobj.parent)
        kobject_child_del(dev->kobj.parent, &dev->kobj);
    dev->kobj.parent = parent ? &parent->kobj : NULL;
    if (dev->kobj.parent)
        kobject_child_add(dev->kobj.parent, &dev->kobj);
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
    if (!dev || !dev->bus)
        return -1;
    if (dev->driver)
        return 0;
    struct device_driver *drv;
    list_for_each_entry(drv, &dev->bus->drivers, bus_list) {
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
    kset_add(device_model_devices_kset(), &dev->kobj);
    INIT_LIST_HEAD(&dev->bus_list);
    INIT_LIST_HEAD(&dev->class_list);
    if (dev->bus)
        list_add_tail(&dev->bus_list, &dev->bus->devices);
    if (dev->class)
        list_add_tail(&dev->class_list, &dev->class->devices);
    if (!dev->kobj.parent) {
        struct device *root = device_model_platform_root();
        if (root && dev != root) {
            if (dev->bus == &platform_bus_type)
                device_set_parent(dev, root);
        }
    }
    devtmpfs_register_device(dev);
    device_uevent_emit("add", dev);
    if (dev->bus)
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
    if (dev->kobj.parent)
        kobject_child_del(dev->kobj.parent, &dev->kobj);
    devtmpfs_unregister_device(dev);
    device_uevent_emit("remove", dev);
    kset_remove(&devices_kset, &dev->kobj);
    kobject_put(&dev->kobj);
    return 0;
}

/* device_register_simple: Implement device register simple. */
struct device *device_register_simple(const char *name, const char *type, struct bus_type *bus, void *data)
{
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->driver_data = data;
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

/* device_register_simple_parent: Implement device register simple parent. */
struct device *device_register_simple_parent(const char *name, const char *type, struct bus_type *bus, struct device *parent, void *data)
{
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->driver_data = data;
    if (parent)
        device_set_parent(dev, parent);
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

/* device_register_simple_class: Implement device register simple class. */
struct device *device_register_simple_class(const char *name, const char *type, struct bus_type *bus, struct class *cls, void *data)
{
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->class = cls;
    dev->driver_data = data;
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

/* device_register_simple_class_parent: Implement device register simple class parent. */
struct device *device_register_simple_class_parent(const char *name, const char *type, struct bus_type *bus, struct class *cls, struct device *parent, void *data)
{
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    device_initialize(dev, name);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->class = cls;
    dev->driver_data = data;
    if (parent)
        device_set_parent(dev, parent);
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

/* device_model_device_count: Implement device model device count. */
uint32_t device_model_device_count(void)
{
    uint32_t n = 0;
    struct kset *kset = device_model_devices_kset();
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        n++;
    return n;
}

/* device_model_device_at: Implement device model device at. */
struct device *device_model_device_at(uint32_t index)
{
    struct kset *kset = device_model_devices_kset();
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
struct device *device_model_find_device(const char *name)
{
    if (!name)
        return NULL;
    struct kset *kset = device_model_devices_kset();
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        if (!strcmp(kobj->name, name))
            return container_of(kobj, struct device, kobj);
    return NULL;
}

/* device_model_devices_kset: Implement device model devices kset. */
struct kset *device_model_devices_kset(void)
{
    return &devices_kset;
}

/* device_model_drivers_kset: Implement device model drivers kset. */
struct kset *device_model_drivers_kset(void)
{
    return &drivers_kset;
}

/* device_model_classes_kset: Implement device model classes kset. */
struct kset *device_model_classes_kset(void)
{
    return &classes_kset;
}

struct kset *device_model_buses_kset(void)
{
    return &buses_kset;
}

struct kobj_type *device_model_device_ktype(void)
{
    return &device_ktype;
}

/* device_model_kset_init: Initialize device model kset. */
void device_model_kset_init(void)
{
    device_sysfs_init_ktype();
    kset_init(&devices_kset, "devices");
    kset_init(&drivers_kset, "drivers");
    kset_init(&classes_kset, "classes");
    kset_init(&buses_kset, "bus");
    if (!virtual_root_dev) {
        struct device *vroot = device_register_simple_parent("virtual", "virtual", NULL, NULL, NULL);
        if (vroot) {
            device_model_set_virtual_root(vroot);
            device_model_virtual_subsys("block");
            device_model_virtual_subsys("tty");
        }
    }
}
