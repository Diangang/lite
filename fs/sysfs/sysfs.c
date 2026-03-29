#include "linux/file.h"
#include "linux/sysfs.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/timer.h"
#include "linux/slab.h"
#include "linux/device.h"

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
static sysfs_class_entry_t sys_class_entries[16];
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

static uint32_t sys_read_kernel_uevent(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    return device_uevent_read(offset, size, buffer);
}

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
    struct device *dev = device_model_find_device(tmp);
    if (!dev)
        return 0;
    if (driver_bind_device(drv, dev) != 0)
        return 0;
    return size;
}

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
    struct device *dev = device_model_find_device(tmp);
    if (!dev || dev->driver != drv)
        return 0;
    device_unbind(dev);
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

            return e;
        }
    }
    return NULL;
}

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
    return NULL;
}

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
    return NULL;
}

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

static struct dirent *sys_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_devices_kset();
    if (!kset)
        return NULL;
    struct kobject *cur = kset->list;
    uint32_t i = 0;
    while (cur) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = 0x6000 + index;
            return &sys_dirent;
        }
        i++;
        cur = cur->next;
    }
    return NULL;
}

static struct inode *sys_devices_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct device *dev = device_model_find_device(name);
    if (!dev)
        return NULL;
    sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
    if (!e)
        return NULL;
    return &e->dir;
}

static struct dirent *sys_bus_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "platform");
        sys_dirent.ino = sys_bus_platform.i_ino;
        return &sys_dirent;
    }
    return NULL;
}

static struct inode *sys_bus_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    if (!strcmp(name, "platform"))
        return &sys_bus_platform;
    return NULL;
}

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

static struct dirent *sys_bus_platform_drivers_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_drivers_kset();
    if (!kset)
        return NULL;
    struct kobject *cur = kset->list;
    uint32_t i = 0;
    while (cur) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = 0x7600 + index;
            return &sys_dirent;
        }
        i++;
        cur = cur->next;
    }
    return NULL;
}

static struct dirent *sys_class_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct kset *kset = device_model_classes_kset();
    if (!kset)
        return NULL;
    struct kobject *cur = kset->list;
    uint32_t i = 0;
    while (cur) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->name);
            sys_dirent.ino = 0x8700 + index;
            return &sys_dirent;
        }
        i++;
        cur = cur->next;
    }
    return NULL;
}

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

static struct dirent *sys_class_dir_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    if (!node)
        return NULL;
    sysfs_class_entry_t *e = (sysfs_class_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->cls)
        return NULL;
    struct device *cur = e->cls->devices;
    uint32_t i = 0;
    while (cur) {
        if (i == index) {
            strcpy(sys_dirent.name, cur->kobj.name);
            sys_dirent.ino = 0x8900 + index;
            return &sys_dirent;
        }
        i++;
        cur = cur->class_next;
    }
    return NULL;
}

static struct inode *sys_class_dir_finddir(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    sysfs_class_entry_t *e = (sysfs_class_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->cls)
        return NULL;
    struct device *cur = e->cls->devices;
    while (cur) {
        if (!strcmp(cur->kobj.name, name)) {
            sysfs_dev_entry_t *de = sysfs_get_device_entry(cur);
            if (!de)
                return NULL;
            de->dir.f_ops = &sys_devdir_ops;
            return &de->dir;
        }
        cur = cur->class_next;
    }
    return NULL;
}

static struct inode *sys_bus_platform_drivers_finddir(struct inode *node, const char *name)
{
    (void)node;
    if (!name)
        return NULL;
    struct kset *kset = device_model_drivers_kset();
    if (!kset)
        return NULL;
    struct kobject *cur = kset->list;
    while (cur) {
        if (!strcmp(cur->name, name)) {
            struct device_driver *drv = CONTAINER_OF(cur, struct device_driver, kobj);
            sysfs_drv_entry_t *e = sysfs_get_driver_entry(drv);
            if (!e)
                return NULL;
            e->dir.f_ops = &sys_drvdir_ops;
            return &e->dir;
        }
        cur = cur->next;
    }
    return NULL;
}

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

static struct file_operations sys_bus_platform_devices_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = sys_devices_readdir,
    .finddir = sys_devices_finddir,
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

void init_sysfs(void)
{
    vfs_mount_fs("/sys", "sysfs");
}

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
    sys_kernel_uevent.i_size = 512;
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

static int init_sysfs_fs(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}
fs_initcall(init_sysfs_fs);
