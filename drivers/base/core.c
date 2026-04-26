#include "linux/device.h"
#include "linux/kernel.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/sysfs.h"
#include "linux/errno.h"
#include "linux/platform_device.h"
#include "linux/pci.h"
#include "linux/serio.h"
#include "linux/virtio.h"
#include "linux/kernel.h"
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
    INIT_LIST_HEAD(&dev->deferred_probe);
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
        put_device(dev);
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
    /* #region debug-point deferred-probe-nvme-core */
    printf("TRAEDBG {\"ev\":\"device_attach_enter\",\"dev\":\"%s\",\"bus\":\"%s\"}\n",
           dev->kobj.name[0] ? dev->kobj.name : "-",
           dev->bus->subsys.kset.kobj.name ? dev->bus->subsys.kset.kobj.name : "-");
    /* #endregion debug-point deferred-probe-nvme-core */
    klist_iter_init(bus_drivers_klist(dev->bus), &iter);
    while ((node = klist_next(&iter)) != NULL) {
        struct device_driver *drv = container_of(node, struct device_driver, knode_bus);
        if (dev->bus->match && dev->bus->match(dev, drv)) {
            int rc = driver_probe_device(drv, dev);
            if (rc == -EPROBE_DEFER) {
                klist_iter_exit(&iter);
                driver_deferred_probe_add(dev);
                /* #region debug-point deferred-probe-nvme-core */
                printf("TRAEDBG {\"ev\":\"device_attach_defer\",\"dev\":\"%s\",\"bus\":\"%s\",\"drv\":\"%s\"}\n",
                       dev->kobj.name[0] ? dev->kobj.name : "-",
                       dev->bus->subsys.kset.kobj.name ? dev->bus->subsys.kset.kobj.name : "-",
                       drv->name ? drv->name : "-");
                /* #endregion debug-point deferred-probe-nvme-core */
                return 0;
            }
            if (rc == 0) {
                klist_iter_exit(&iter);
                /* #region debug-point deferred-probe-nvme-core */
                printf("TRAEDBG {\"ev\":\"device_attach_bound\",\"dev\":\"%s\",\"bus\":\"%s\",\"drv\":\"%s\"}\n",
                       dev->kobj.name[0] ? dev->kobj.name : "-",
                       dev->bus->subsys.kset.kobj.name ? dev->bus->subsys.kset.kobj.name : "-",
                       drv->name ? drv->name : "-");
                /* #endregion debug-point deferred-probe-nvme-core */
                return 0;
            }
        }
    }
    klist_iter_exit(&iter);
    /* #region debug-point deferred-probe-nvme-core */
    printf("TRAEDBG {\"ev\":\"device_attach_nomatch\",\"dev\":\"%s\",\"bus\":\"%s\"}\n",
           dev->kobj.name[0] ? dev->kobj.name : "-",
           dev->bus->subsys.kset.kobj.name ? dev->bus->subsys.kset.kobj.name : "-");
    /* #endregion debug-point deferred-probe-nvme-core */
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
    struct device *held;
    int ret;

    /*
     * Linux allows devices without an associated bus (bus == NULL),
     * e.g. many class devices. Keep this for learning parity.
     */
    if (!dev)
        return -1;
    held = get_device(dev);
    if (!held)
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
    ret = devtmpfs_create_node(dev);
    if (ret != 0)
        goto err_links;
    device_uevent_emit("add", dev);
    if (dev->bus && bus_drivers_autoprobe(dev->bus))
        device_attach(dev);
    put_device(held);
    return 0;

err_links:
    device_sysfs_remove_links(dev);
    if (dev->class && dev->class_list.next && dev->class_list.prev)
        list_del_init(&dev->class_list);
    if (dev->bus && klist_node_attached(&dev->knode_bus))
        klist_remove(&dev->knode_bus);
    kset_remove(devices_kset, &dev->kobj);
    kobject_del(&dev->kobj);
    put_device(held);
    return ret;
}

/* device_register: Implement device register. */
int device_register(struct device *dev)
{
    return device_add(dev);
}

int device_del(struct device *dev)
{
    if (!dev)
        return -1;
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
    return 0;
}

/* device_unregister: Implement device unregister. */
void device_unregister(struct device *dev)
{
    if (!dev)
        return;
    device_del(dev);
    put_device(dev);
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

/*
 * Linux mapping: uevent support lives in drivers/base/core.c (no separate uevent.c
 * file in linux2.6/). Lite keeps a tiny in-kernel buffer for the last uevent(s)
 * to power /sys-style inspection.
 */

static char uevent_buf[4096];
static uint32_t uevent_len;
extern uint32_t uevent_seqnum;

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
    snprintf(tmp, sizeof(tmp), "%u", v);
    return buf_append(buf, off, cap, tmp);
}

static int buf_append_u32_oct(char *buf, uint32_t *off, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t pos = 0;

    if (value == 0)
        tmp[pos++] = '0';
    else {
        char rev[16];
        uint32_t rev_pos = 0;
        while (value && rev_pos < sizeof(rev)) {
            rev[rev_pos++] = (char)('0' + (value & 7u));
            value >>= 3;
        }
        while (rev_pos)
            tmp[pos++] = rev[--rev_pos];
    }
    tmp[pos] = 0;
    return buf_append(buf, off, cap, tmp);
}

static void u32_to_hex_fixed(uint32_t v, char *out, uint32_t width)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < width; i++) {
        uint32_t shift = (width - 1 - i) * 4;
        out[i] = hex[(v >> shift) & 0xFu];
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
    uint32_t pre = 4;
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
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "pci")) {
        struct pci_dev *pdev = pci_get_pci_dev(dev);
        if (!pdev)
            return -1;
        uint32_t off = 0;
        char hx[8];
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "pci:v"))
            return -1;
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
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "platform")) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "platform:"))
            return -1;
        if (!buf_append(buf, &off, cap, dev->kobj.name))
            return -1;
        return 0;
    }
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "virtio")) {
        /* Linux mapping: drivers/virtio/virtio.c exports "virtio:d%08Xv%08X". */
        struct virtio_device *vdev = dev_to_virtio(dev);
        if (!vdev)
            return -1;
        uint32_t off = 0;
        char hx[16];
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "virtio:d"))
            return -1;
        u32_to_hex_fixed(vdev->id.device, hx, 8);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append_ch(buf, &off, cap, 'v'))
            return -1;
        u32_to_hex_fixed(vdev->id.vendor, hx, 8);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        return 0;
    }
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "serio")) {
        /* Linux mapping: drivers/input/serio/serio.c modalias_show(). */
        struct serio *serio = to_serio_port(dev);
        snprintf(buf, cap, "serio:ty%02Xpr%02Xid%02Xex%02X",
                 serio->id.type, serio->id.proto, serio->id.id, serio->id.extra);
        return 0;
    }
    if (dev->bus) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, dev->bus->subsys.kset.kobj.name))
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

void device_uevent_emit(const char *action, struct device *dev)
{
    if (!action || !dev || !dev->kobj.name[0])
        return;

    char tmp[512];
    char devpath[256];
    char modalias[128];
    const char *devnode;
    uint32_t mode = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t off = 0;
    tmp[0] = 0;
    devpath[0] = 0;
    modalias[0] = 0;

    device_get_devpath(dev, devpath, sizeof(devpath));
    device_get_modalias(dev, modalias, sizeof(modalias));

    if (!buf_append(tmp, &off, sizeof(tmp), "ACTION=") ||
        !buf_append(tmp, &off, sizeof(tmp), action) ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "DEVPATH=") ||
        !buf_append(tmp, &off, sizeof(tmp), devpath[0] ? devpath : "/devices") ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "SUBSYSTEM="))
        return;
    if (dev->class) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->class->subsys.kset.kobj.name))
            return;
    } else if (dev->bus) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->bus->subsys.kset.kobj.name))
            return;
    } else {
        if (!buf_append(tmp, &off, sizeof(tmp), "unknown"))
            return;
    }
    if (!buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (modalias[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MODALIAS=") ||
            !buf_append(tmp, &off, sizeof(tmp), modalias) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    devnode = device_get_devnode(dev, &mode, &uid, &gid);
    if (devnode && devnode[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVNAME=") ||
            !buf_append(tmp, &off, sizeof(tmp), devnode) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVMODE=") ||
            !buf_append_u32_oct(tmp, &off, sizeof(tmp), mode & 0777u) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVUID=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), uid) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVGID=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), gid) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (dev->devt) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MAJOR=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), MAJOR(dev->devt)) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "MINOR=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), MINOR(dev->devt)) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (!buf_append(tmp, &off, sizeof(tmp), "SEQNUM=") ||
        !buf_append_u32_dec(tmp, &off, sizeof(tmp), ++uevent_seqnum) ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
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

uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!buffer || offset >= uevent_len)
        return 0;
    uint32_t remain = uevent_len - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, uevent_buf + offset, size);
    return size;
}

uint32_t device_uevent_seqnum(void)
{
    return uevent_seqnum;
}

/*
 * Linux mapping: virtual devices are managed by driver-core (no separate
 * drivers/base/virtual.c in linux2.6/). Lite uses this to anchor pure sysfs
 * devices.
 */

static struct device *virtual_root;

struct child_name_match {
    const char *name;
    struct device *match;
};

static int match_child_name(struct device *child, void *data)
{
    struct child_name_match *match = (struct child_name_match *)data;
    if (!child || !match || !match->name)
        return 0;
    if (strcmp(child->kobj.name, match->name))
        return 0;
    match->match = child;
    return 1;
}

struct device *virtual_child_device(struct device *vroot, const char *name)
{
    if (!vroot || !name)
        return NULL;
    struct child_name_match match = {
        .name = name,
        .match = NULL,
    };
    (void)device_for_each_child(vroot, &match, match_child_name);
    return match.match;
}

static struct device *ensure_virtual_root_device(void)
{
    if (virtual_root)
        return virtual_root;
    virtual_root = (struct device *)kmalloc(sizeof(*virtual_root));
    if (!virtual_root)
        return NULL;
    memset(virtual_root, 0, sizeof(*virtual_root));
    device_initialize(virtual_root, "virtual");
    if (device_register(virtual_root) != 0) {
        kobject_put(&virtual_root->kobj);
        virtual_root = NULL;
        return NULL;
    }
    return virtual_root;
}

struct device *virtual_root_device(void)
{
    return ensure_virtual_root_device();
}
