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
static char uevent_buf[512];
static uint32_t uevent_len = 0;

static void device_release_kobj(struct kobject *kobj)
{
    struct device *dev = CONTAINER_OF(kobj, struct device, kobj);
    kfree(dev);
}

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

int device_rebind(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    struct device_driver *drv = dev->bus->drivers;
    while (drv) {
        if (dev->bus->match && dev->bus->match(dev, drv)) {
            dev->driver = drv;
            if (drv->probe && drv->probe(dev) != 0) {
                dev->driver = NULL;
                drv = drv->next;
                continue;
            }
            device_uevent_emit("bind", dev);
            return 0;
        }
        drv = drv->next;
    }
    return 0;
}

int device_register(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    kset_add(device_model_devices_kset(), &dev->kobj);
    dev->kobj.next = (struct kobject*)dev->bus->devices;
    dev->bus->devices = dev;
    if (dev->class) {
        dev->class_next = dev->class->devices;
        dev->class->devices = dev;
    }
    devtmpfs_register_device(dev);
    device_uevent_emit("add", dev);
    device_rebind(dev);
    return 0;
}

int device_unregister(struct device *dev)
{
    if (!dev || !dev->bus)
        return -1;
    device_unbind(dev);
    struct device *prev = NULL;
    struct device *cur = dev->bus->devices;
    while (cur) {
        if (cur == dev) {
            if (prev)
                prev->kobj.next = cur->kobj.next;
            else
                dev->bus->devices = (struct device*)cur->kobj.next;
            if (dev->class) {
                struct device *cprev = NULL;
                struct device *ccur = dev->class->devices;
                while (ccur) {
                    if (ccur == dev) {
                        if (cprev)
                            cprev->class_next = ccur->class_next;
                        else
                            dev->class->devices = ccur->class_next;
                        break;
                    }
                    cprev = ccur;
                    ccur = ccur->class_next;
                }
            }
            devtmpfs_unregister_device(dev);
            device_uevent_emit("remove", dev);
            kset_remove(&devices_kset, &dev->kobj);
            kobject_put(&dev->kobj);
            return 0;
        }
        prev = cur;
        cur = (struct device*)cur->kobj.next;
    }
    return -1;
}

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

uint32_t device_model_device_count(void)
{
    if (!devmodel_ready)
        return 0;
    uint32_t n = 0;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        n++;
        d = (struct device*)d->kobj.next;
    }
    return n;
}

struct device *device_model_device_at(uint32_t index)
{
    if (!devmodel_ready)
        return NULL;
    uint32_t i = 0;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        if (i == index)
            return d;
        i++;
        d = (struct device*)d->kobj.next;
    }
    return NULL;
}

struct device *device_model_find_device(const char *name)
{
    if (!devmodel_ready || !name)
        return NULL;
    struct device *d = device_model_platform_bus()->devices;
    while (d) {
        if (!strcmp(d->kobj.name, name))
            return d;
        d = (struct device*)d->kobj.next;
    }
    return NULL;
}

struct kset *device_model_devices_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &devices_kset;
}

struct kset *device_model_drivers_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &drivers_kset;
}

struct kset *device_model_classes_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &classes_kset;
}

int class_register(struct class *cls)
{
    if (!cls)
        return -1;
    kset_add(device_model_classes_kset(), &cls->kobj);
    return 0;
}

int class_unregister(struct class *cls)
{
    if (!cls)
        return -1;
    kset_remove(&classes_kset, &cls->kobj);
    kobject_put(&cls->kobj);
    return 0;
}

struct class *class_find(const char *name)
{
    if (!devmodel_ready || !name)
        return NULL;
    struct kobject *cur = classes_kset.list;
    while (cur) {
        if (!strcmp(cur->name, name))
            return CONTAINER_OF(cur, struct class, kobj);
        cur = cur->next;
    }
    return NULL;
}

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
    if (off + uevent_len >= sizeof(uevent_buf))
        uevent_len = 0;
    memcpy(uevent_buf + uevent_len, tmp, off);
    uevent_len += off;
}

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

int device_model_inited(void)
{
    return devmodel_ready;
}

void device_model_mark_inited(void)
{
    devmodel_ready = 1;
}

void device_model_kset_init(void)
{
    kset_init(&devices_kset, "devices");
    kset_init(&drivers_kset, "drivers");
    kset_init(&classes_kset, "classes");
}
