#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/timer.h"
#include "linux/slab.h"
#include "linux/device.h"
#include "linux/tty.h"
#include "linux/blkdev.h"

#define OFFSETOF(type, member) ((uint32_t)(&((type*)0)->member))
#define CONTAINER_OF(ptr, type, member) ((type*)((uint8_t*)(ptr) - OFFSETOF(type, member)))
#include "linux/kobject.h"

static struct dirent sys_dirent;
// Note: sys_root is dynamically allocated in init_sysfs now
static struct inode sys_kernel;
static struct inode sys_devices;
static struct inode sys_bus;
static struct inode sys_class;
static struct inode sys_bus_platform;
static struct inode sys_bus_platform_devices;
static struct inode sys_bus_platform_drivers;
static struct inode sys_bus_pci;
static struct inode sys_bus_pci_devices;
static struct inode sys_bus_pci_drivers;
static struct inode sys_kernel_version;
static struct inode sys_kernel_uptime;
static struct inode sys_kernel_uevent;
typedef struct sysfs_dev_entry {
    int used;
    struct device *dev;
    struct inode dir;
    struct inode f_type;
    struct inode f_bus;
    struct inode f_driver;
    struct inode f_attr0;
    struct inode f_attr1;
    struct inode f_attr2;
} sysfs_dev_entry_t;
static sysfs_dev_entry_t sys_dev_entries[16];
typedef struct sysfs_drv_entry {
    int used;
    struct device_driver *drv;
    struct inode dir;
    struct inode f_name;
    struct inode f_bind;
    struct inode f_unbind;
} sysfs_drv_entry_t;
static sysfs_drv_entry_t sys_drv_entries[16];
typedef struct sysfs_class_entry {
    int used;
    struct class *cls;
    struct inode dir;
} sysfs_class_entry_t;
/* sys_read_kernel_version: Implement sys read kernel version. */
static sysfs_class_entry_t sys_class_entries[16];
typedef struct sysfs_bus_entry {
    int used;
    struct bus_type *bus;
    struct inode dir;
    struct inode devices_dir;
    struct inode drivers_dir;
} sysfs_bus_entry_t;
static sysfs_bus_entry_t sys_bus_entries[16];
static struct file_operations sys_class_dir_ops;

static uint32_t sys_read_kernel_version(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static const char *text = "lite-os 0.2\n";
    uint32_t n = (uint32_t)strlen(text);
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, text + offset, size);
    return size;
}

/* sys_read_kernel_uptime: Implement sys read kernel uptime. */
static uint32_t sys_read_kernel_uptime(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static char tmp[64];
    uint32_t off = 0;
    uint32_t ticks = timer_get_ticks();
    itoa((int)ticks, 10, tmp);
    off = (uint32_t)strlen(tmp);
    if (off + 1 < sizeof(tmp)) {
        tmp[off++] = '\n';
        tmp[off] = 0;
    }
    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* sys_read_kernel_uevent: Implement sys read kernel uevent. */
static uint32_t sys_read_kernel_uevent(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    return device_uevent_read(offset, size, buffer);
}

/* sys_read_device_type: Implement sys read device type. */
static uint32_t sys_read_device_type(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device*)(uintptr_t)node->impl;
    const char *text = (dev && dev->type) ? dev->type : "unknown";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* sys_read_device_bus: Implement sys read device bus. */
static uint32_t sys_read_device_bus(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device*)(uintptr_t)node->impl;
    const char *text = (dev && dev->bus) ? dev->bus->kobj.name : "none";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* sys_read_device_driver: Implement sys read device driver. */
static uint32_t sys_read_device_driver(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device*)(uintptr_t)node->impl;
    const char *text = (dev && dev->driver) ? dev->driver->kobj.name : "unbound";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* sys_read_driver_name: Implement sys read driver name. */
static uint32_t sys_read_driver_name(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device_driver *drv = (struct device_driver*)(uintptr_t)node->impl;
    const char *text = drv ? drv->kobj.name : "unknown";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp))
        n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* sys_write_driver_bind: Implement sys write driver bind. */
static uint32_t sys_write_driver_bind(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !buffer || offset)
        return 0;
    struct device_driver *drv = (struct device_driver*)(uintptr_t)node->impl;
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
    struct bus_type *bus = drv->bus;
    if (!bus)
        return 0;
    struct device *dev = NULL;
    struct device *cur;
    list_for_each_entry(cur, &bus->devices, bus_list) {
        if (!strcmp(cur->kobj.name, tmp)) {
            dev = cur;
            break;
        }
    }
    if (!dev)
        return 0;
    if (driver_probe_device(drv, dev) != 0)
        return 0;
    return size;
}

/* sys_write_driver_unbind: Implement sys write driver unbind. */
static uint32_t sys_write_driver_unbind(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !buffer || offset)
        return 0;
    struct device_driver *drv = (struct device_driver*)(uintptr_t)node->impl;
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
    struct bus_type *bus = drv->bus;
    if (!bus)
        return 0;
    struct device *dev = NULL;
    struct device *cur;
    list_for_each_entry(cur, &bus->devices, bus_list) {
        if (!strcmp(cur->kobj.name, tmp)) {
            dev = cur;
            break;
        }
    }
    if (!dev || dev->driver != drv)
        return 0;
    device_release_driver(dev);
    return size;
}

static struct dirent *sys_devdir_readdir(struct file *file, uint32_t index);
static struct inode *sys_devdir_finddir(struct inode *node, const char *name);

static struct file_operations sys_devdir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_devdir_readdir,
    .finddir = sys_devdir_finddir,
    .ioctl = NULL
};

static struct file_operations sys_read_device_type_ops = {
    .read = sys_read_device_type,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_read_device_bus_ops = {
    .read = sys_read_device_bus,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_read_device_driver_ops = {
    .read = sys_read_device_driver,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static uint32_t sys_read_device_attr0(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device *)(uintptr_t)node->impl;
    char tmp[64];
    const char *text = "none";
    uint32_t value = 0;
    int use_num = 0;

    if (dev && dev->type && !strcmp(dev->type, "tty")) {
        struct tty_device *ttydev = tty_device_from_dev(dev);
        if (ttydev && ttydev->driver)
            text = ttydev->driver->name;
    } else if (dev && dev->type && !strcmp(dev->type, "block")) {
        struct gendisk *disk = gendisk_from_dev(dev);
        if (disk && disk->bdev) {
            value = disk->bdev->block_size ? (disk->bdev->size / disk->bdev->block_size) : 0;
            use_num = 1;
        }
    }

    uint32_t n = 0;
    if (use_num) {
        itoa((int)value, 10, tmp);
        n = (uint32_t)strlen(tmp);
    } else {
        n = (uint32_t)strlen(text);
        if (n + 1 >= sizeof(tmp))
            n = sizeof(tmp) - 2;
        memcpy(tmp, text, n);
    }
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t sys_read_device_attr1(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device *)(uintptr_t)node->impl;
    char tmp[64];
    const char *text = "none";
    uint32_t value = 0;
    int use_num = 0;

    if (dev && dev->type && !strcmp(dev->type, "tty")) {
        struct tty_device *ttydev = tty_device_from_dev(dev);
        if (ttydev) {
            value = ttydev->index;
            use_num = 1;
        }
    } else if (dev && dev->type && !strcmp(dev->type, "block")) {
        struct gendisk *disk = gendisk_from_dev(dev);
        if (disk && disk->bdev && disk->bdev->queue)
            text = "present";
        else
            text = "none";
    }

    uint32_t n = 0;
    if (use_num) {
        itoa((int)value, 10, tmp);
        n = (uint32_t)strlen(tmp);
    } else {
        n = (uint32_t)strlen(text);
        if (n + 1 >= sizeof(tmp))
            n = sizeof(tmp) - 2;
        memcpy(tmp, text, n);
    }
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct file_operations sys_read_device_attr0_ops = {
    .read = sys_read_device_attr0,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_read_device_attr1_ops = {
    .read = sys_read_device_attr1,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static const char *sys_device_attr0_name(struct device *dev)
{
    if (!dev || !dev->type)
        return NULL;
    if (!strcmp(dev->type, "tty"))
        return "tty_driver";
    if (!strcmp(dev->type, "block"))
        return "capacity";
    return NULL;
}

static const char *sys_device_attr1_name(struct device *dev)
{
    if (!dev || !dev->type)
        return NULL;
    if (!strcmp(dev->type, "tty"))
        return "index";
    if (!strcmp(dev->type, "block"))
        return "queue";
    return NULL;
}

static const char *sys_device_attr2_name(struct device *dev)
{
    if (!dev || !dev->type)
        return NULL;
    if (!strcmp(dev->type, "tty"))
        return "dev";
    if (!strcmp(dev->type, "block"))
        return "dev";
    return NULL;
}

static uint32_t sys_read_device_attr2(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer)
        return 0;
    struct device *dev = (struct device *)(uintptr_t)node->impl;
    char tmp[64];
    uint32_t off = 0;

    if (dev && dev->type && !strcmp(dev->type, "tty")) {
        struct tty_device *ttydev = tty_device_from_dev(dev);
        uint32_t major = ttydev ? ttydev->major : 0;
        uint32_t minor = ttydev ? ttydev->minor : 0;
        itoa((int)major, 10, tmp);
        off = (uint32_t)strlen(tmp);
        if (off + 1 < sizeof(tmp)) {
            tmp[off++] = ':';
            itoa((int)minor, 10, tmp + off);
            off = (uint32_t)strlen(tmp);
            if (off + 1 < sizeof(tmp)) {
                tmp[off++] = '\n';
                tmp[off] = 0;
            }
        }
    } else if (dev && dev->type && !strcmp(dev->type, "block")) {
        struct gendisk *disk = gendisk_from_dev(dev);
        uint32_t major = disk ? disk->major : 0;
        uint32_t minor = disk ? disk->minor : 0;
        itoa((int)major, 10, tmp);
        off = (uint32_t)strlen(tmp);
        if (off + 1 < sizeof(tmp)) {
            tmp[off++] = ':';
            itoa((int)minor, 10, tmp + off);
            off = (uint32_t)strlen(tmp);
            if (off + 1 < sizeof(tmp)) {
                tmp[off++] = '\n';
                tmp[off] = 0;
            }
        }
    } else {
        const char *text = "0:0\n";
        off = (uint32_t)strlen(text);
        memcpy(tmp, text, off + 1);
    }

    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct file_operations sys_read_device_attr2_ops = {
    .read = sys_read_device_attr2,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_bus_entry_ops;
static struct file_operations sys_bus_devices_ops;
static struct file_operations sys_bus_drivers_ops;
static struct file_operations sys_drvdir_ops;

static struct file_operations sys_read_driver_name_ops = {
    .read = sys_read_driver_name,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_write_driver_bind_ops = {
    .read = NULL,
    .write = sys_write_driver_bind,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_write_driver_unbind_ops = {
    .read = NULL,
    .write = sys_write_driver_unbind,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

/* sysfs_get_device_entry: Implement sysfs get device entry. */
static sysfs_dev_entry_t *sysfs_get_device_entry(struct device *dev)
{
    if (!dev)
        return NULL;
    for (uint32_t i = 0; i < (sizeof(sys_dev_entries) / sizeof(sys_dev_entries[0])); i++) {
        if (sys_dev_entries[i].used && sys_dev_entries[i].dev == dev)
            return &sys_dev_entries[i];
    }
    for (uint32_t i = 0; i < (sizeof(sys_dev_entries) / sizeof(sys_dev_entries[0])); i++) {
        if (!sys_dev_entries[i].used) {
            sysfs_dev_entry_t *e = &sys_dev_entries[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->dev = dev;

            memset(&e->dir, 0, sizeof(e->dir));
            e->dir.flags = FS_DIRECTORY;
            e->dir.i_ino = 0x7000 + i;
            e->dir.f_ops = &sys_devdir_ops;
            e->dir.impl = (uint32_t)(uintptr_t)e;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.i_mode = 0555;

            memset(&e->f_type, 0, sizeof(e->f_type));
            e->f_type.flags = FS_FILE;
            e->f_type.i_ino = 0x7100 + i;
            e->f_type.i_size = 64;
            e->f_type.f_ops = &sys_read_device_type_ops;
            e->f_type.impl = (uint32_t)(uintptr_t)dev;
            e->f_type.uid = 0;
            e->f_type.gid = 0;
            e->f_type.i_mode = 0444;

            memset(&e->f_bus, 0, sizeof(e->f_bus));
            e->f_bus.flags = FS_FILE;
            e->f_bus.i_ino = 0x7200 + i;
            e->f_bus.i_size = 64;
            e->f_bus.f_ops = &sys_read_device_bus_ops;
            e->f_bus.impl = (uint32_t)(uintptr_t)dev;
            e->f_bus.uid = 0;
            e->f_bus.gid = 0;
            e->f_bus.i_mode = 0444;

            memset(&e->f_driver, 0, sizeof(e->f_driver));
            e->f_driver.flags = FS_FILE;
            e->f_driver.i_ino = 0x7300 + i;
            e->f_driver.i_size = 64;
            e->f_driver.f_ops = &sys_read_device_driver_ops;
            e->f_driver.impl = (uint32_t)(uintptr_t)dev;
            e->f_driver.uid = 0;
            e->f_driver.gid = 0;
            e->f_driver.i_mode = 0444;

            memset(&e->f_attr0, 0, sizeof(e->f_attr0));
            e->f_attr0.flags = FS_FILE;
            e->f_attr0.i_ino = 0x7A00 + i;
            e->f_attr0.i_size = 64;
            e->f_attr0.f_ops = &sys_read_device_attr0_ops;
            e->f_attr0.impl = (uint32_t)(uintptr_t)dev;
            e->f_attr0.uid = 0;
            e->f_attr0.gid = 0;
            e->f_attr0.i_mode = 0444;

            memset(&e->f_attr1, 0, sizeof(e->f_attr1));
            e->f_attr1.flags = FS_FILE;
            e->f_attr1.i_ino = 0x7B00 + i;
            e->f_attr1.i_size = 64;
            e->f_attr1.f_ops = &sys_read_device_attr1_ops;
            e->f_attr1.impl = (uint32_t)(uintptr_t)dev;
            e->f_attr1.uid = 0;
            e->f_attr1.gid = 0;
            e->f_attr1.i_mode = 0444;

            memset(&e->f_attr2, 0, sizeof(e->f_attr2));
            e->f_attr2.flags = FS_FILE;
            e->f_attr2.i_ino = 0x7C00 + i;
            e->f_attr2.i_size = 64;
            e->f_attr2.f_ops = &sys_read_device_attr2_ops;
            e->f_attr2.impl = (uint32_t)(uintptr_t)dev;
            e->f_attr2.uid = 0;
            e->f_attr2.gid = 0;
            e->f_attr2.i_mode = 0444;

            return e;
        }
    }
    return NULL;
}

/* sysfs_get_driver_entry: Implement sysfs get driver entry. */
static sysfs_drv_entry_t *sysfs_get_driver_entry(struct device_driver *drv)
{
    if (!drv)
        return NULL;
    for (uint32_t i = 0; i < (sizeof(sys_drv_entries) / sizeof(sys_drv_entries[0])); i++) {
        if (sys_drv_entries[i].used && sys_drv_entries[i].drv == drv)
            return &sys_drv_entries[i];
    }
    for (uint32_t i = 0; i < (sizeof(sys_drv_entries) / sizeof(sys_drv_entries[0])); i++) {
        if (!sys_drv_entries[i].used) {
            sysfs_drv_entry_t *e = &sys_drv_entries[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->drv = drv;

            memset(&e->dir, 0, sizeof(e->dir));
            e->dir.flags = FS_DIRECTORY;
            e->dir.i_ino = 0x7400 + i;
            e->dir.f_ops = &sys_drvdir_ops;
            e->dir.impl = (uint32_t)(uintptr_t)e;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.i_mode = 0555;

            memset(&e->f_name, 0, sizeof(e->f_name));
            e->f_name.flags = FS_FILE;
            e->f_name.i_ino = 0x7500 + i;
            e->f_name.i_size = 64;
            e->f_name.f_ops = &sys_read_driver_name_ops;
            e->f_name.impl = (uint32_t)(uintptr_t)drv;
            e->f_name.uid = 0;
            e->f_name.gid = 0;
            e->f_name.i_mode = 0444;

            memset(&e->f_bind, 0, sizeof(e->f_bind));
            e->f_bind.flags = FS_FILE;
            e->f_bind.i_ino = 0x7600 + i;
            e->f_bind.i_size = 64;
            e->f_bind.f_ops = &sys_write_driver_bind_ops;
            e->f_bind.impl = (uint32_t)(uintptr_t)drv;
            e->f_bind.uid = 0;
            e->f_bind.gid = 0;
            e->f_bind.i_mode = 0222;

            memset(&e->f_unbind, 0, sizeof(e->f_unbind));
            e->f_unbind.flags = FS_FILE;
            e->f_unbind.i_ino = 0x7700 + i;
            e->f_unbind.i_size = 64;
            e->f_unbind.f_ops = &sys_write_driver_unbind_ops;
            e->f_unbind.impl = (uint32_t)(uintptr_t)drv;
            e->f_unbind.uid = 0;
            e->f_unbind.gid = 0;
            e->f_unbind.i_mode = 0222;

            return e;
        }
    }
    return NULL;
}

/* sysfs_get_class_entry: Implement sysfs get class entry. */
static sysfs_class_entry_t *sysfs_get_class_entry(struct class *cls)
{
    if (!cls)
        return NULL;
    for (int i = 0; i < 16; i++) {
        if (sys_class_entries[i].used && sys_class_entries[i].cls == cls)
            return &sys_class_entries[i];
    }
    for (int i = 0; i < 16; i++) {
        if (!sys_class_entries[i].used) {
            sysfs_class_entry_t *e = &sys_class_entries[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->cls = cls;
            e->dir.flags = FS_DIRECTORY;
            e->dir.i_ino = 0x8800 + i;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.i_mode = 0555;
            e->dir.impl = (uint32_t)(uintptr_t)e;
            return e;
        }
    }
    return NULL;
}

static sysfs_bus_entry_t *sysfs_get_bus_entry(struct bus_type *bus)
{
    if (!bus)
        return NULL;
    for (int i = 0; i < 16; i++) {
        if (sys_bus_entries[i].used && sys_bus_entries[i].bus == bus)
            return &sys_bus_entries[i];
    }
    for (int i = 0; i < 16; i++) {
        if (!sys_bus_entries[i].used) {
            sysfs_bus_entry_t *e = &sys_bus_entries[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->bus = bus;

            e->dir.flags = FS_DIRECTORY;
            e->dir.i_ino = 0x8A00 + i;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.i_mode = 0555;
            e->dir.impl = (uint32_t)(uintptr_t)e;
            e->dir.f_ops = &sys_bus_entry_ops;

            e->devices_dir.flags = FS_DIRECTORY;
            e->devices_dir.i_ino = 0x8B00 + i;
            e->devices_dir.uid = 0;
            e->devices_dir.gid = 0;
            e->devices_dir.i_mode = 0555;
            e->devices_dir.impl = (uint32_t)(uintptr_t)e;
            e->devices_dir.f_ops = &sys_bus_devices_ops;

            e->drivers_dir.flags = FS_DIRECTORY;
            e->drivers_dir.i_ino = 0x8C00 + i;
            e->drivers_dir.uid = 0;
            e->drivers_dir.gid = 0;
            e->drivers_dir.i_mode = 0555;
            e->drivers_dir.impl = (uint32_t)(uintptr_t)e;
            e->drivers_dir.f_ops = &sys_bus_drivers_ops;

            return e;
        }
    }
    return NULL;
}

/* sys_devdir_readdir: Implement sys devdir readdir. */
static struct dirent *sys_devdir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_dev_entry_t *e = (sysfs_dev_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used)
        return NULL;
    if (index == 0) {
        strcpy(sys_dirent.name, "type");
        sys_dirent.ino = e->f_type.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "bus");
        sys_dirent.ino = e->f_bus.i_ino;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "driver");
        sys_dirent.ino = e->f_driver.i_ino;
        return &sys_dirent;
    }
    const char *attr0 = sys_device_attr0_name(e->dev);
    const char *attr1 = sys_device_attr1_name(e->dev);
    const char *attr2 = sys_device_attr2_name(e->dev);
    uint32_t next_index = 3;
    if (attr0 && index == next_index) {
        strcpy(sys_dirent.name, attr0);
        sys_dirent.ino = e->f_attr0.i_ino;
        return &sys_dirent;
    }
    if (attr0)
        next_index++;
    if (attr1 && index == next_index) {
        strcpy(sys_dirent.name, attr1);
        sys_dirent.ino = e->f_attr1.i_ino;
        return &sys_dirent;
    }
    if (attr1)
        next_index++;
    if (attr2 && index == next_index) {
        strcpy(sys_dirent.name, attr2);
        sys_dirent.ino = e->f_attr2.i_ino;
        return &sys_dirent;
    }
    if (attr2)
        next_index++;
    if (index == next_index && e->dev && e->dev->kobj.parent) {
        struct device *parent_dev = CONTAINER_OF(e->dev->kobj.parent, struct device, kobj);
        sysfs_dev_entry_t *pe = sysfs_get_device_entry(parent_dev);
        if (!pe)
            return NULL;
        strcpy(sys_dirent.name, "parent");
        sys_dirent.ino = pe->dir.i_ino;
        return &sys_dirent;
    }
    struct kobject *child = e->dev ? e->dev->kobj.children : NULL;
    uint32_t i = 0;
    uint32_t child_base = next_index + (e->dev && e->dev->kobj.parent ? 1 : 0);
    while (child) {
        if (i == index - child_base) {
            struct device *child_dev = CONTAINER_OF(child, struct device, kobj);
            sysfs_dev_entry_t *ce = sysfs_get_device_entry(child_dev);
            if (!ce)
                return NULL;
            strcpy(sys_dirent.name, child->name);
            sys_dirent.ino = ce->dir.i_ino;
            return &sys_dirent;
        }
        i++;
        child = child->next;
    }
    return NULL;
}

/* sys_devdir_finddir: Implement sys devdir finddir. */
static struct inode *sys_devdir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_dev_entry_t *e = (sysfs_dev_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used)
        return NULL;
    if (!strcmp(name, "type"))
        return &e->f_type;
    if (!strcmp(name, "bus"))
        return &e->f_bus;
    if (!strcmp(name, "driver"))
        return &e->f_driver;
    const char *attr0 = sys_device_attr0_name(e->dev);
    const char *attr1 = sys_device_attr1_name(e->dev);
    const char *attr2 = sys_device_attr2_name(e->dev);
    if (attr0 && !strcmp(name, attr0))
        return &e->f_attr0;
    if (attr1 && !strcmp(name, attr1))
        return &e->f_attr1;
    if (attr2 && !strcmp(name, attr2))
        return &e->f_attr2;
    if (!strcmp(name, "parent") && e->dev && e->dev->kobj.parent) {
        struct device *parent_dev = CONTAINER_OF(e->dev->kobj.parent, struct device, kobj);
        sysfs_dev_entry_t *pe = sysfs_get_device_entry(parent_dev);
        if (!pe)
            return NULL;
        return &pe->dir;
    }
    struct kobject *child = e->dev ? e->dev->kobj.children : NULL;
    while (child) {
        if (!strcmp(child->name, name)) {
            struct device *child_dev = CONTAINER_OF(child, struct device, kobj);
            sysfs_dev_entry_t *ce = sysfs_get_device_entry(child_dev);
            if (!ce)
                return NULL;
            return &ce->dir;
        }
        child = child->next;
    }
    return NULL;
}

/* sys_drvdir_readdir: Implement sys drvdir readdir. */
static struct dirent *sys_drvdir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_drv_entry_t *e = (sysfs_drv_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used)
        return NULL;
    if (index == 0) {
        strcpy(sys_dirent.name, "name");
        sys_dirent.ino = e->f_name.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "bind");
        sys_dirent.ino = e->f_bind.i_ino;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "unbind");
        sys_dirent.ino = e->f_unbind.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_drvdir_finddir: Implement sys drvdir finddir. */
static struct inode *sys_drvdir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_drv_entry_t *e = (sysfs_drv_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used)
        return NULL;
    if (!strcmp(name, "name"))
        return &e->f_name;
    if (!strcmp(name, "bind"))
        return &e->f_bind;
    if (!strcmp(name, "unbind"))
        return &e->f_unbind;
    return NULL;
}

static struct file_operations sys_drvdir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_drvdir_readdir,
    .finddir = sys_drvdir_finddir,
    .ioctl = NULL
};

/* sys_kernel_readdir: Implement sys kernel readdir. */
static struct dirent *sys_kernel_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "version");
        sys_dirent.ino = sys_kernel_version.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "uptime");
        sys_dirent.ino = sys_kernel_uptime.i_ino;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "uevent");
        sys_dirent.ino = sys_kernel_uevent.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_kernel_finddir: Implement sys kernel finddir. */
static struct inode *sys_kernel_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "version"))
        return &sys_kernel_version;
    if (!strcmp(name, "uptime"))
        return &sys_kernel_uptime;
    if (!strcmp(name, "uevent"))
        return &sys_kernel_uevent;
    return NULL;
}

/* sys_devices_readdir: Implement sys devices readdir. */
static struct dirent *sys_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (cur->parent)
            continue;
        if (i == index) {
            struct device *dev = CONTAINER_OF(cur, struct device, kobj);
            sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
            if (!e)
                return NULL;
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = e->dir.i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_devices_finddir: Implement sys devices finddir. */
static struct inode *sys_devices_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (cur->parent)
            continue;
        if (!strcmp(cur->name, name)) {
            struct device *dev = CONTAINER_OF(cur, struct device, kobj);
            sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
            if (!e)
                return NULL;
            return &e->dir;
        }
    }
    return NULL;
}

/* sys_bus_readdir: Implement sys bus readdir. */
static struct dirent *sys_bus_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct bus_type *bus = bus_at(index);
    if (!bus)
        return NULL;
    sysfs_bus_entry_t *e = sysfs_get_bus_entry(bus);
    if (!e)
        return NULL;
    strcpy(sys_dirent.name, bus->kobj.name);
    sys_dirent.ino = e->dir.i_ino;
    return &sys_dirent;
    return NULL;
}

/* sys_bus_finddir: Implement sys bus finddir. */
static struct inode *sys_bus_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = bus_find(name);
    if (!bus)
        return NULL;
    sysfs_bus_entry_t *e = sysfs_get_bus_entry(bus);
    if (!e)
        return NULL;
    return &e->dir;
    return NULL;
}

static struct dirent *sys_bus_entry_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    if (index == 0) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = e->devices_dir.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "drivers");
        sys_dirent.ino = e->drivers_dir.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

static struct inode *sys_bus_entry_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    if (!strcmp(name, "devices"))
        return &e->devices_dir;
    if (!strcmp(name, "drivers"))
        return &e->drivers_dir;
    return NULL;
}

static struct dirent *sys_bus_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    uint32_t i = 0;
    struct device *dev;
    list_for_each_entry(dev, &e->bus->devices, bus_list) {
        if (dev->type && !strcmp(dev->type, "platform-root"))
            continue;
        if (i == index) {
            sysfs_dev_entry_t *de = sysfs_get_device_entry(dev);
            if (!de)
                return NULL;
            strcpy(sys_dirent.name, dev->kobj.name);
            sys_dirent.ino = de->dir.i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

static struct inode *sys_bus_devices_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    struct device *dev;
    list_for_each_entry(dev, &e->bus->devices, bus_list) {
        if (!strcmp(dev->kobj.name, name)) {
            sysfs_dev_entry_t *de = sysfs_get_device_entry(dev);
            if (!de)
                return NULL;
            de->dir.f_ops = &sys_devdir_ops;
            return &de->dir;
        }
    }
    return NULL;
}

static struct dirent *sys_bus_drivers_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    uint32_t i = 0;
    struct device_driver *drv;
    list_for_each_entry(drv, &e->bus->drivers, bus_list) {
        if (i == index) {
            sysfs_drv_entry_t *de = sysfs_get_driver_entry(drv);
            if (!de)
                return NULL;
            strcpy(sys_dirent.name, drv->kobj.name);
            sys_dirent.ino = de->dir.i_ino;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

static struct inode *sys_bus_drivers_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_bus_entry_t *e = (sysfs_bus_entry_t *)(uintptr_t)node->impl;
    if (!e || !e->bus)
        return NULL;
    struct device_driver *drv;
    list_for_each_entry(drv, &e->bus->drivers, bus_list) {
        if (!strcmp(drv->kobj.name, name)) {
            sysfs_drv_entry_t *de = sysfs_get_driver_entry(drv);
            if (!de)
                return NULL;
            de->dir.f_ops = &sys_drvdir_ops;
            return &de->dir;
        }
    }
    return NULL;
}

static struct file_operations sys_bus_entry_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_entry_readdir,
    .finddir = sys_bus_entry_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_devices_readdir,
    .finddir = sys_bus_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_drivers_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_drivers_readdir,
    .finddir = sys_bus_drivers_finddir,
    .ioctl = NULL
};

/* sys_bus_platform_readdir: Implement sys bus platform readdir. */
static struct dirent *sys_bus_platform_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = sys_bus_platform_devices.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "drivers");
        sys_dirent.ino = sys_bus_platform_drivers.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_bus_platform_finddir: Implement sys bus platform finddir. */
static struct inode *sys_bus_platform_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "devices"))
        return &sys_bus_platform_devices;
    if (!strcmp(name, "drivers"))
        return &sys_bus_platform_drivers;
    return NULL;
}

/* sys_bus_pci_readdir: Implement sys bus PCI readdir. */
static struct dirent *sys_bus_pci_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = sys_bus_pci_devices.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "drivers");
        sys_dirent.ino = sys_bus_pci_drivers.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_bus_pci_finddir: Implement sys bus PCI finddir. */
static struct inode *sys_bus_pci_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "devices"))
        return &sys_bus_pci_devices;
    if (!strcmp(name, "drivers"))
        return &sys_bus_pci_drivers;
    return NULL;
}

/* sys_bus_platform_devices_readdir: Implement sys bus platform devices readdir. */
static struct dirent *sys_bus_platform_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct bus_type *bus = device_model_platform_bus();
    if (!bus)
        return NULL;
    uint32_t i = 0;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && !strcmp(dev->type, "platform-root"))
            continue;
        if (i == index) {
            strcpy(sys_dirent.name, dev->kobj.name);
            sys_dirent.ino = 0x6100 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_bus_platform_devices_finddir: Implement sys bus platform devices finddir. */
static struct inode *sys_bus_platform_devices_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = device_model_platform_bus();
    if (!bus)
        return NULL;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && !strcmp(dev->type, "platform-root"))
            continue;
        if (!strcmp(dev->kobj.name, name)) {
            sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
            if (!e)
                return NULL;
            return &e->dir;
        }
    }
    return NULL;
}

/* sys_bus_pci_devices_readdir: Implement sys bus PCI devices readdir. */
static struct dirent *sys_bus_pci_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct bus_type *bus = device_model_pci_bus();
    if (!bus)
        return NULL;
    uint32_t i = 0;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && !strcmp(dev->type, "pci-root"))
            continue;
        if (i == index) {
            strcpy(sys_dirent.name, dev->kobj.name);
            sys_dirent.ino = 0x6200 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_bus_pci_devices_finddir: Implement sys bus PCI devices finddir. */
static struct inode *sys_bus_pci_devices_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = device_model_pci_bus();
    if (!bus)
        return NULL;
    struct device *dev;
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->type && !strcmp(dev->type, "pci-root"))
            continue;
        if (!strcmp(dev->kobj.name, name)) {
            sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
            if (!e)
                return NULL;
            return &e->dir;
        }
    }
    return NULL;
}

/* sys_bus_platform_drivers_readdir: Implement sys bus platform drivers readdir. */
static struct dirent *sys_bus_platform_drivers_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct bus_type *bus = device_model_platform_bus();
    if (!bus)
        return NULL;
    uint32_t i = 0;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (i == index) {
            strcpy(sys_dirent.name, drv->kobj.name);
            sys_dirent.ino = 0x7700 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_bus_pci_drivers_readdir: Implement sys bus PCI drivers readdir. */
static struct dirent *sys_bus_pci_drivers_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct bus_type *bus = device_model_pci_bus();
    if (!bus)
        return NULL;
    uint32_t i = 0;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (i == index) {
            strcpy(sys_dirent.name, drv->kobj.name);
            sys_dirent.ino = 0x7a00 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_class_readdir: Implement sys class readdir. */
static struct dirent *sys_class_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_classes_kset();
    if (!kset)
        return NULL;
    uint32_t i = 0;
    struct kobject *cur;
    list_for_each_entry(cur, &kset->list, entry) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = 0x8700 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_class_finddir: Implement sys class finddir. */
static struct inode *sys_class_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct class *cls = class_find(name);
    if (!cls)
        return NULL;
    sysfs_class_entry_t *e = sysfs_get_class_entry(cls);
    if (!e)
        return NULL;
    e->dir.f_ops = &sys_class_dir_ops;
    return &e->dir;
}

/* sys_class_dir_readdir: Implement sys class dir readdir. */
static struct dirent *sys_class_dir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_class_entry_t *e = (sysfs_class_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->cls)
        return NULL;
    uint32_t i = 0;
    struct device *cur;
    list_for_each_entry(cur, &e->cls->devices, class_list) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->kobj.name);
            sys_dirent.ino = 0x8900 + index;
            return &sys_dirent;
        }
        i++;
    }
    return NULL;
}

/* sys_class_dir_finddir: Implement sys class dir finddir. */
static struct inode *sys_class_dir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_class_entry_t *e = (sysfs_class_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->cls)
        return NULL;
    struct device *cur;
    list_for_each_entry(cur, &e->cls->devices, class_list) {
        if (!strcmp(cur->kobj.name, name)) {
            sysfs_dev_entry_t *de = sysfs_get_device_entry(cur);
            if (!de)
                return NULL;
            de->dir.f_ops = &sys_devdir_ops;
            return &de->dir;
        }
    }
    return NULL;
}

/* sys_bus_platform_drivers_finddir: Implement sys bus platform drivers finddir. */
static struct inode *sys_bus_platform_drivers_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = device_model_platform_bus();
    if (!bus)
        return NULL;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (!strcmp(drv->kobj.name, name)) {
            sysfs_drv_entry_t *e = sysfs_get_driver_entry(drv);
            if (!e)
                return NULL;
            e->dir.f_ops = &sys_drvdir_ops;
            return &e->dir;
        }
    }
    return NULL;
}

/* sys_bus_pci_drivers_finddir: Implement sys bus PCI drivers finddir. */
static struct inode *sys_bus_pci_drivers_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct bus_type *bus = device_model_pci_bus();
    if (!bus)
        return NULL;
    struct device_driver *drv;
    list_for_each_entry(drv, &bus->drivers, bus_list) {
        if (!strcmp(drv->kobj.name, name)) {
            sysfs_drv_entry_t *e = sysfs_get_driver_entry(drv);
            if (!e)
                return NULL;
            e->dir.f_ops = &sys_drvdir_ops;
            return &e->dir;
        }
    }
    return NULL;
}

/* sys_readdir: Implement sys readdir. */
static struct dirent *sys_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "kernel");
        sys_dirent.ino = sys_kernel.i_ino;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = sys_devices.i_ino;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "bus");
        sys_dirent.ino = sys_bus.i_ino;
        return &sys_dirent;
    }
    if (index == 3) {
        strcpy(sys_dirent.name, "class");
        sys_dirent.ino = sys_class.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

/* sys_finddir: Implement sys finddir. */
static struct inode *sys_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "kernel"))
        return &sys_kernel;
    if (!strcmp(name, "devices"))
        return &sys_devices;
    if (!strcmp(name, "bus"))
        return &sys_bus;
    if (!strcmp(name, "class"))
        return &sys_class;
    return NULL;
}

static struct file_operations sys_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_readdir,
    .finddir = sys_finddir,
    .ioctl = NULL
};

static struct file_operations sys_kernel_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_kernel_readdir,
    .finddir = sys_kernel_finddir,
    .ioctl = NULL
};

static struct file_operations sys_devices_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_devices_readdir,
    .finddir = sys_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_readdir,
    .finddir = sys_bus_finddir,
    .ioctl = NULL
};

static struct file_operations sys_class_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_dir_readdir,
    .finddir = sys_class_dir_finddir,
    .ioctl = NULL
};

static struct file_operations sys_class_root_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_class_readdir,
    .finddir = sys_class_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_platform_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_platform_readdir,
    .finddir = sys_bus_platform_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_pci_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_pci_readdir,
    .finddir = sys_bus_pci_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_platform_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_platform_devices_readdir,
    .finddir = sys_bus_platform_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_pci_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_pci_devices_readdir,
    .finddir = sys_bus_pci_devices_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_platform_drivers_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_platform_drivers_readdir,
    .finddir = sys_bus_platform_drivers_finddir,
    .ioctl = NULL
};

static struct file_operations sys_bus_pci_drivers_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_bus_pci_drivers_readdir,
    .finddir = sys_bus_pci_drivers_finddir,
    .ioctl = NULL
};

static struct file_operations sys_read_kernel_version_ops = {
    .read = sys_read_kernel_version,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_read_kernel_uptime_ops = {
    .read = sys_read_kernel_uptime,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct file_operations sys_read_kernel_uevent_ops = {
    .read = sys_read_kernel_uevent,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

/* init_sysfs: Initialize sysfs. */
void init_sysfs(void)
{
    vfs_mount_fs("/sys", "sysfs");
}

/* sysfs_fill_super: Implement sysfs fill super. */
static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)data;
    (void)silent;

    struct inode *sys_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!sys_root)
        return -1;

    memset(sys_dev_entries, 0, sizeof(sys_dev_entries));
    memset(sys_drv_entries, 0, sizeof(sys_drv_entries));
    memset(sys_class_entries, 0, sizeof(sys_class_entries));
    memset(sys_root, 0, sizeof(struct inode));
    sys_root->flags = FS_DIRECTORY;
    sys_root->i_ino = 1;
    sys_root->f_ops = &sys_dir_ops;
    sys_root->uid = 0;
    sys_root->gid = 0;
    sys_root->i_mode = 0555;

    memset(&sys_kernel, 0, sizeof(sys_kernel));
    sys_kernel.flags = FS_DIRECTORY;
    sys_kernel.i_ino = 2;
    sys_kernel.f_ops = &sys_kernel_dir_ops;
    sys_kernel.uid = 0;
    sys_kernel.gid = 0;
    sys_kernel.i_mode = 0555;

    memset(&sys_devices, 0, sizeof(sys_devices));
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.i_ino = 3;
    sys_devices.f_ops = &sys_devices_dir_ops;
    sys_devices.uid = 0;
    sys_devices.gid = 0;
    sys_devices.i_mode = 0555;

    memset(&sys_bus, 0, sizeof(sys_bus));
    sys_bus.flags = FS_DIRECTORY;
    sys_bus.i_ino = 4;
    sys_bus.f_ops = &sys_bus_dir_ops;
    sys_bus.uid = 0;
    sys_bus.gid = 0;
    sys_bus.i_mode = 0555;

    memset(&sys_class, 0, sizeof(sys_class));
    sys_class.flags = FS_DIRECTORY;
    sys_class.i_ino = 5;
    sys_class.f_ops = &sys_class_root_ops;
    sys_class.uid = 0;
    sys_class.gid = 0;
    sys_class.i_mode = 0555;

    memset(&sys_bus_platform, 0, sizeof(sys_bus_platform));
    sys_bus_platform.flags = FS_DIRECTORY;
    sys_bus_platform.i_ino = 6;
    sys_bus_platform.f_ops = &sys_bus_platform_dir_ops;
    sys_bus_platform.uid = 0;
    sys_bus_platform.gid = 0;
    sys_bus_platform.i_mode = 0555;

    memset(&sys_bus_platform_devices, 0, sizeof(sys_bus_platform_devices));
    sys_bus_platform_devices.flags = FS_DIRECTORY;
    sys_bus_platform_devices.i_ino = 7;
    sys_bus_platform_devices.f_ops = &sys_bus_platform_devices_ops;
    sys_bus_platform_devices.uid = 0;
    sys_bus_platform_devices.gid = 0;
    sys_bus_platform_devices.i_mode = 0555;

    memset(&sys_bus_platform_drivers, 0, sizeof(sys_bus_platform_drivers));
    sys_bus_platform_drivers.flags = FS_DIRECTORY;
    sys_bus_platform_drivers.i_ino = 8;
    sys_bus_platform_drivers.f_ops = &sys_bus_platform_drivers_ops;
    sys_bus_platform_drivers.uid = 0;
    sys_bus_platform_drivers.gid = 0;
    sys_bus_platform_drivers.i_mode = 0555;

    memset(&sys_bus_pci, 0, sizeof(sys_bus_pci));
    sys_bus_pci.flags = FS_DIRECTORY;
    sys_bus_pci.i_ino = 12;
    sys_bus_pci.f_ops = &sys_bus_pci_dir_ops;
    sys_bus_pci.uid = 0;
    sys_bus_pci.gid = 0;
    sys_bus_pci.i_mode = 0555;

    memset(&sys_bus_pci_devices, 0, sizeof(sys_bus_pci_devices));
    sys_bus_pci_devices.flags = FS_DIRECTORY;
    sys_bus_pci_devices.i_ino = 13;
    sys_bus_pci_devices.f_ops = &sys_bus_pci_devices_ops;
    sys_bus_pci_devices.uid = 0;
    sys_bus_pci_devices.gid = 0;
    sys_bus_pci_devices.i_mode = 0555;

    memset(&sys_bus_pci_drivers, 0, sizeof(sys_bus_pci_drivers));
    sys_bus_pci_drivers.flags = FS_DIRECTORY;
    sys_bus_pci_drivers.i_ino = 14;
    sys_bus_pci_drivers.f_ops = &sys_bus_pci_drivers_ops;
    sys_bus_pci_drivers.uid = 0;
    sys_bus_pci_drivers.gid = 0;
    sys_bus_pci_drivers.i_mode = 0555;

    memset(&sys_kernel_version, 0, sizeof(sys_kernel_version));
    sys_kernel_version.flags = FS_FILE;
    sys_kernel_version.i_ino = 9;
    sys_kernel_version.i_size = 64;
    sys_kernel_version.f_ops = &sys_read_kernel_version_ops;
    sys_kernel_version.uid = 0;
    sys_kernel_version.gid = 0;
    sys_kernel_version.i_mode = 0444;

    memset(&sys_kernel_uptime, 0, sizeof(sys_kernel_uptime));
    sys_kernel_uptime.flags = FS_FILE;
    sys_kernel_uptime.i_ino = 10;
    sys_kernel_uptime.i_size = 64;
    sys_kernel_uptime.f_ops = &sys_read_kernel_uptime_ops;
    sys_kernel_uptime.uid = 0;
    sys_kernel_uptime.gid = 0;
    sys_kernel_uptime.i_mode = 0444;

    memset(&sys_kernel_uevent, 0, sizeof(sys_kernel_uevent));
    sys_kernel_uevent.flags = FS_FILE;
    sys_kernel_uevent.i_ino = 11;
    sys_kernel_uevent.i_size = 4096;
    sys_kernel_uevent.f_ops = &sys_read_kernel_uevent_ops;
    sys_kernel_uevent.uid = 0;
    sys_kernel_uevent.gid = 0;
    sys_kernel_uevent.i_mode = 0444;

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = sys_root;

    return 0;
}

static struct file_system_type sysfs_fs_type = {
    .name = "sysfs",
    .get_sb = vfs_get_sb_single,
    .fill_super = sysfs_fill_super,
    .kill_sb = NULL,
    .next = NULL,
};

/* init_sysfs_fs: Initialize sysfs fs. */
static int init_sysfs_fs(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}
fs_initcall(init_sysfs_fs);
