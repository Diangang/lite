#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/devtmpfs.h"
#include "linux/errno.h"
#include "linux/pci.h"
#include "linux/tty.h"
#include "linux/blkdev.h"

static int devmodel_ready = 0;
static struct kset devices_kset;
static struct kset drivers_kset;
static struct kset classes_kset;
static struct kset buses_kset;
static char uevent_buf[4096];
static uint32_t uevent_len = 0;
static struct device *platform_root_dev = NULL;
static struct device *virtual_root_dev = NULL;
static struct device *virtual_subsys_devs[16];
static uint32_t virtual_subsys_count = 0;
static struct device *deferred_devs[64];
static uint32_t deferred_devs_count = 0;

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

static int buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || !s)
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (*off + n >= cap)
        return 0;
    memcpy(buf + *off, s, n);
    *off += n;
    buf[*off] = 0;
    return 1;
}

static int buf_append_ch(char *buf, uint32_t *off, uint32_t cap, char c)
{
    if (!buf || !off)
        return 0;
    if (*off + 1 >= cap)
        return 0;
    buf[(*off)++] = c;
    buf[*off] = 0;
    return 1;
}

static int buf_append_u32_dec(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 10, tmp);
    return buf_append(buf, off, cap, tmp);
}

static void u32_to_hex_fixed(uint32_t v, char *out, uint32_t width)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < width; i++) {
        uint32_t shift = (width - 1 - i) * 4;
        out[i] = hex[(v >> shift) & 0xF];
    }
    out[width] = 0;
}

static int kobject_build_path(struct kobject *kobj, const char *prefix, char *buf, uint32_t cap)
{
    if (!kobj || !prefix || !buf || cap == 0)
        return -1;
    const char *stack[16];
    uint32_t depth = 0;
    struct kobject *cur = kobj;
    while (cur && depth < (uint32_t)(sizeof(stack) / sizeof(stack[0]))) {
        stack[depth++] = cur->name;
        cur = cur->parent;
    }
    uint32_t off = 0;
    uint32_t pre = (uint32_t)strlen(prefix);
    if (pre + 1 >= cap)
        return -1;
    memcpy(buf + off, prefix, pre);
    off += pre;
    for (int i = (int)depth - 1; i >= 0; i--) {
        const char *name = stack[i];
        if (!name || !*name)
            continue;
        uint32_t n = (uint32_t)strlen(name);
        if (off + 1 + n + 1 >= cap)
            return -1;
        buf[off++] = '/';
        memcpy(buf + off, name, n);
        off += n;
    }
    buf[off] = 0;
    return 0;
}

int device_get_devpath(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev)
        return -1;
    return kobject_build_path(&dev->kobj, "/devices", buf, cap);
}

int device_get_sysfs_path(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev || !buf || cap == 0)
        return -1;
    char devpath[256];
    if (device_get_devpath(dev, devpath, sizeof(devpath)) != 0)
        return -1;
    uint32_t pre = 4; /* "/sys" */
    uint32_t dlen = (uint32_t)strlen(devpath);
    if (pre + dlen + 1 > cap)
        return -1;
    memcpy(buf, "/sys", pre);
    memcpy(buf + pre, devpath, dlen + 1);
    return 0;
}

int device_get_modalias(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev || !buf || cap == 0)
        return -1;
    if (dev->bus && !strcmp(dev->bus->kobj.name, "pci")) {
        /* Minimal PCI modalias: pci:vVVVVdDDDDbcCCscSS */
        struct pci_dev *pdev = pci_get_pci_dev(dev);
        if (!pdev)
            return -1;
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "pci:v"))
            return -1;
        char hx[8];
        u32_to_hex_fixed((uint32_t)pdev->vendor, hx, 4);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append_ch(buf, &off, cap, 'd'))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->device, hx, 4);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append(buf, &off, cap, "bc"))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->class, hx, 2);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append(buf, &off, cap, "sc"))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->subclass, hx, 2);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        return 0;
    }
    if (dev->bus && !strcmp(dev->bus->kobj.name, "platform")) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "platform:"))
            return -1;
        if (!buf_append(buf, &off, cap, dev->kobj.name))
            return -1;
        return 0;
    }
    if (dev->bus) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, dev->bus->kobj.name))
            return -1;
        if (!buf_append_ch(buf, &off, cap, ':'))
            return -1;
        if (!buf_append(buf, &off, cap, dev->kobj.name))
            return -1;
        return 0;
    }
    uint32_t off = 0;
    buf[0] = 0;
    if (!buf_append(buf, &off, cap, "device:"))
        return -1;
    if (!buf_append(buf, &off, cap, dev->kobj.name))
        return -1;
    return 0;
}

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

struct device *device_model_virtual_root(void)
{
    return virtual_root_dev;
}

void device_model_set_virtual_root(struct device *dev)
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
    d = device_register_simple_class_parent(name, "virtual", NULL, NULL, vr, NULL);
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
    if (!dev->release)
        dev->release = device_default_release;
}

static struct attribute dev_attr_type = { .name = "type", .mode = 0444 };
static struct attribute dev_attr_bus = { .name = "bus", .mode = 0444 };
static struct attribute dev_attr_driver = { .name = "driver", .mode = 0444 };
static struct attribute dev_attr_modalias = { .name = "modalias", .mode = 0444 };
static struct attribute dev_attr_dev = { .name = "dev", .mode = 0444 };
static struct attribute dev_attr_index = { .name = "index", .mode = 0444 };
static struct attribute dev_attr_tty_driver = { .name = "tty_driver", .mode = 0444 };
static struct attribute dev_attr_capacity = { .name = "capacity", .mode = 0444 };
static struct attribute dev_attr_queue = { .name = "queue", .mode = 0444 };

static const struct attribute *device_default_attrs[] = {
    &dev_attr_type,
    &dev_attr_bus,
    &dev_attr_driver,
    &dev_attr_modalias,
    &dev_attr_dev,
    &dev_attr_index,
    &dev_attr_tty_driver,
    &dev_attr_capacity,
    &dev_attr_queue,
    NULL,
};

static uint32_t device_attr_is_visible(struct kobject *kobj, const struct attribute *attr)
{
    if (!kobj || !attr || !attr->name)
        return 0;
    struct device *dev = container_of(kobj, struct device, kobj);
    if (!dev || !dev->type)
        return attr->mode;

    if (!strcmp(attr->name, "index") || !strcmp(attr->name, "tty_driver"))
        return !strcmp(dev->type, "tty") ? attr->mode : 0;
    if (!strcmp(attr->name, "capacity") || !strcmp(attr->name, "queue"))
        return !strcmp(dev->type, "block") ? attr->mode : 0;
    if (!strcmp(attr->name, "dev"))
        return (!strcmp(dev->type, "tty") || !strcmp(dev->type, "block")) ? attr->mode : 0;

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

static uint32_t sysfs_emit_u32_line(char *buffer, uint32_t cap, uint32_t value)
{
    if (!buffer || cap < 2)
        return 0;
    itoa((int)value, 10, buffer);
    uint32_t n = (uint32_t)strlen(buffer);
    if (n + 1 >= cap)
        return 0;
    buffer[n++] = '\n';
    buffer[n] = 0;
    return n;
}

static uint32_t device_sysfs_show(struct kobject *kobj, const struct attribute *attr, char *buffer, uint32_t cap)
{
    if (!kobj || !attr || !attr->name || !buffer)
        return 0;
    struct device *dev = container_of(kobj, struct device, kobj);

    if (!strcmp(attr->name, "type")) {
        const char *text = (dev && dev->type) ? dev->type : "unknown";
        return sysfs_emit_text_line(buffer, cap, text);
    }
    if (!strcmp(attr->name, "bus")) {
        const char *text = (dev && dev->bus) ? dev->bus->kobj.name : "none";
        return sysfs_emit_text_line(buffer, cap, text);
    }
    if (!strcmp(attr->name, "driver")) {
        const char *text = (dev && dev->driver) ? dev->driver->kobj.name : "unbound";
        return sysfs_emit_text_line(buffer, cap, text);
    }
    if (!strcmp(attr->name, "modalias")) {
        if (cap == 0)
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
    if (!strcmp(attr->name, "dev")) {
        if (dev && dev->type && !strcmp(dev->type, "tty")) {
            struct tty_port *ttydev = tty_port_from_dev(dev);
            uint32_t major = ttydev ? ttydev->major : 0;
            uint32_t minor = ttydev ? ttydev->minor : 0;
            return sysfs_emit_devno_line(buffer, cap, major, minor);
        }
        if (dev && dev->type && !strcmp(dev->type, "block")) {
            struct gendisk *disk = gendisk_from_dev(dev);
            uint32_t major = disk ? disk->major : 0;
            uint32_t minor = disk ? disk->minor : 0;
            return sysfs_emit_devno_line(buffer, cap, major, minor);
        }
        return sysfs_emit_text_line(buffer, cap, "0:0");
    }
    if (!strcmp(attr->name, "tty_driver")) {
        const char *text = "none";
        if (dev && dev->type && !strcmp(dev->type, "tty")) {
            struct tty_port *ttydev = tty_port_from_dev(dev);
            if (ttydev && ttydev->driver)
                text = ttydev->driver->name;
        }
        return sysfs_emit_text_line(buffer, cap, text);
    }
    if (!strcmp(attr->name, "index")) {
        struct tty_port *ttydev = (dev && dev->type && !strcmp(dev->type, "tty")) ? tty_port_from_dev(dev) : NULL;
        uint32_t idx = ttydev ? ttydev->index : 0;
        return sysfs_emit_u32_line(buffer, cap, idx);
    }
    if (!strcmp(attr->name, "capacity")) {
        struct gendisk *disk = (dev && dev->type && !strcmp(dev->type, "block")) ? gendisk_from_dev(dev) : NULL;
        uint32_t capv = 0;
        if (disk && disk->bdev)
            capv = disk->bdev->block_size ? (disk->bdev->size / disk->bdev->block_size) : 0;
        return sysfs_emit_u32_line(buffer, cap, capv);
    }
    if (!strcmp(attr->name, "queue")) {
        const char *text = "none";
        struct gendisk *disk = (dev && dev->type && !strcmp(dev->type, "block")) ? gendisk_from_dev(dev) : NULL;
        if (disk && disk->bdev && disk->bdev->queue)
            text = "present";
        return sysfs_emit_text_line(buffer, cap, text);
    }
    return 0;
}

static uint32_t device_sysfs_store(struct kobject *kobj, const struct attribute *attr, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    (void)kobj;
    (void)attr;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0;
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
            struct bus_type *platform = device_model_platform_bus();
            if (platform && dev->bus == platform)
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
    if (!bus)
        return NULL;
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
    if (!bus)
        return NULL;
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
            return container_of(kobj, struct device, kobj);
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
            return container_of(kobj, struct device, kobj);
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

struct kset *device_model_buses_kset(void)
{
    if (!devmodel_ready)
        return NULL;
    return &buses_kset;
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
            return container_of(cur, struct class, kobj);
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
    char tmp[512];
    uint32_t off = 0;
    tmp[0] = 0;

    char devpath[256];
    char modalias[128];
    devpath[0] = 0;
    modalias[0] = 0;
    device_get_devpath(dev, devpath, sizeof(devpath));
    device_get_modalias(dev, modalias, sizeof(modalias));

    if (!buf_append(tmp, &off, sizeof(tmp), "ACTION=") || !buf_append(tmp, &off, sizeof(tmp), action) || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "DEVPATH=") || !buf_append(tmp, &off, sizeof(tmp), devpath[0] ? devpath : "/devices") || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "SUBSYSTEM="))
        return;
    if (dev->class) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->class->kobj.name))
            return;
    } else if (dev->bus) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->bus->kobj.name))
            return;
    } else {
        if (!buf_append(tmp, &off, sizeof(tmp), "unknown"))
            return;
    }
    if (!buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (modalias[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MODALIAS=") || !buf_append(tmp, &off, sizeof(tmp), modalias) || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (dev->devnode_name && dev->devnode_name[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVNAME=") || !buf_append(tmp, &off, sizeof(tmp), dev->devnode_name) || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (dev->dev_major || dev->dev_minor) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MAJOR=") || !buf_append_u32_dec(tmp, &off, sizeof(tmp), dev->dev_major) || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "MINOR=") || !buf_append_u32_dec(tmp, &off, sizeof(tmp), dev->dev_minor) || !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (!buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;

    if (off >= sizeof(uevent_buf) || off >= sizeof(tmp))
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
    device_sysfs_init_ktype();
    kset_init(&devices_kset, "devices");
    kset_init(&drivers_kset, "drivers");
    kset_init(&classes_kset, "classes");
    kset_init(&buses_kset, "bus");
}
