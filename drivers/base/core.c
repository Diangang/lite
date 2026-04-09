#include "linux/device.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/devtmpfs.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))

static int devmodel_ready = 0;
static struct kset devices_kset;
static struct kset drivers_kset;
static struct kset classes_kset;
static char uevent_buf[4096];
static uint32_t uevent_len = 0;
static struct device *platform_root_dev = NULL;

/* device_model_platform_root: Implement device model platform root. */
struct device *device_model_platform_root(void)
{
    return platform_root_dev;
}

/* device_model_set_platform_root: Implement device model set platform root. */
void device_model_set_platform_root(struct device *dev)
{
    platform_root_dev = dev;
}

/* device_release_kobj: Implement device release kobj. */
static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = CONTAINER_OF(kobj, struct device, kobj);
    kfree(dev);
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
            dev->driver = drv;
            if (drv->probe && drv->probe(dev) != 0) {
                dev->driver = NULL;
                continue;
            }
            device_uevent_emit("bind", dev);
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
    if (!dev || !dev->bus)
        return -1;
    kset_add(device_model_devices_kset(), &dev->kobj);
    INIT_LIST_HEAD(&dev->bus_list);
    INIT_LIST_HEAD(&dev->class_list);
    list_add_tail(&dev->bus_list, &dev->bus->devices);
    if (dev->class)
        list_add_tail(&dev->class_list, &dev->class->devices);
    if (!dev->kobj.parent) {
        struct device *root = device_model_platform_root();
        if (root && dev != root) {
            struct bus_type *platform = device_model_platform_bus();
            if (platform && dev->bus == platform)
                device_set_parent(dev, root);
        }
    }
    devtmpfs_register_device(dev);
    device_uevent_emit("add", dev);
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
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    if (dev->bus_list.next && dev->bus_list.prev)
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
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
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
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
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
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
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
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
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

/* device_register_simple_full: Implement device register simple full. */
struct device *device_register_simple_full(const char *name, const char *type, struct bus_type *bus, struct class *cls, void *data, uint16_t vendor_id, uint16_t device_id, uint8_t class_id, uint8_t subclass_id, uint8_t prog_if, uint8_t revision, uint8_t bus_num, uint8_t dev_num, uint8_t func_num)
{
    if (!bus)
        return NULL;
    struct device *dev = (struct device*)kmalloc(sizeof(struct device));
    if (!dev)
        return NULL;
    memset(dev, 0, sizeof(*dev));
    kobject_init(&dev->kobj, name, device_release_kobj);
    dev->type = type;
    dev->bus = bus;
    dev->driver = NULL;
    dev->class = cls;
    dev->driver_data = data;
    dev->vendor_id = vendor_id;
    dev->device_id = device_id;
    dev->class_id = class_id;
    dev->subclass_id = subclass_id;
    dev->prog_if = prog_if;
    dev->revision = revision;
    dev->bus_num = bus_num;
    dev->dev_num = dev_num;
    dev->func_num = func_num;
    if (device_register(dev) != 0) {
        kobject_put(&dev->kobj);
        return NULL;
    }
    return dev;
}

/* device_model_device_count: Implement device model device count. */
uint32_t device_model_device_count(void)
{
    if (!devmodel_ready)
        return 0;
    uint32_t n = 0;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return 0;
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        n++;
    return n;
}

/* device_model_device_at: Implement device model device at. */
struct device *device_model_device_at(uint32_t index)
{
    if (!devmodel_ready)
        return NULL;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry) {
        if (i == index)
            return CONTAINER_OF(kobj, struct device, kobj);
        i++;
    }
    return NULL;
}

/* device_model_find_device: Implement device model find device. */
struct device *device_model_find_device(const char *name)
{
    if (!devmodel_ready || !name)
        return NULL;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    struct kobject *kobj;
    list_for_each_entry(kobj, &kset->list, entry)
        if (!strcmp(kobj->name, name))
            return CONTAINER_OF(kobj, struct device, kobj);
    return NULL;
}

/* device_model_devices_kset: Implement device model devices kset. */
struct kset *device_model_devices_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &devices_kset;
}

/* device_model_drivers_kset: Implement device model drivers kset. */
struct kset *device_model_drivers_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &drivers_kset;
}

/* device_model_classes_kset: Implement device model classes kset. */
struct kset *device_model_classes_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &classes_kset;
}

/* class_register: Implement class register. */
int class_register(struct class *cls)
{
    if (!cls)
        return -1;
    kset_add(device_model_classes_kset(), &cls->kobj);
    return 0;
}

/* class_unregister: Implement class unregister. */
int class_unregister(struct class *cls)
{
    if (!cls)
        return -1;
    kset_remove(&classes_kset, &cls->kobj);
    kobject_put(&cls->kobj);
    return 0;
}

/* class_find: Implement class find. */
struct class *class_find(const char *name)
{
    if (!devmodel_ready || !name)
        return NULL;
    struct kobject *cur;
    list_for_each_entry(cur, &classes_kset.list, entry) {
        if (!strcmp(cur->name, name))
            return CONTAINER_OF(cur, struct class, kobj);
    }
    return NULL;
}

/* device_uevent_emit: Implement device uevent emit. */
void device_uevent_emit(const char *action, struct device *dev)
{
    if (!action || !dev)
        return;
    if (!dev->kobj.name[0])
        return;
    char tmp[80];
    uint32_t off = 0;
    uint32_t act_len = (uint32_t)strlen(action);
    if (act_len >= sizeof(tmp))
        return;
    memcpy(tmp + off, action, act_len);
    off += act_len;
    if (off + 1 >= sizeof(tmp))
        return;
    tmp[off++] = ' ';
    uint32_t name_len = (uint32_t)strlen(dev->kobj.name);
    if (off + name_len + 2 >= sizeof(tmp))
        name_len = sizeof(tmp) - off - 2;
    memcpy(tmp + off, dev->kobj.name, name_len);
    off += name_len;
    tmp[off++] = '\n';
    tmp[off] = 0;
    if (off >= sizeof(uevent_buf))
        return;
    if (off + uevent_len > sizeof(uevent_buf)) {
        uint32_t need = (off + uevent_len) - (uint32_t)sizeof(uevent_buf);
        if (need >= uevent_len) {
            uevent_len = 0;
        } else {
            uint32_t new_len = uevent_len - need;
            for (uint32_t i = 0; i < new_len; i++)
                uevent_buf[i] = uevent_buf[need + i];
            uevent_len = new_len;
        }
    }
    memcpy(uevent_buf + uevent_len, tmp, off);
    uevent_len += off;
}

/* device_uevent_read: Implement device uevent read. */
uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!buffer)
        return 0;
    if (offset >= uevent_len)
        return 0;
    uint32_t remain = uevent_len - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, uevent_buf + offset, size);
    return size;
}

/* device_model_inited: Implement device model inited. */
int device_model_inited(void)
{
    return devmodel_ready;
}

/* device_model_mark_inited: Implement device model mark inited. */
void device_model_mark_inited(void)
{
    devmodel_ready = 1;
}

/* device_model_kset_init: Initialize device model kset. */
void device_model_kset_init(void)
{
    kset_init(&devices_kset, "devices");
    kset_init(&drivers_kset, "drivers");
    kset_init(&classes_kset, "classes");
}
