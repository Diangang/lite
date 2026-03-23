#include "file.h"
#include "sysfs.h"
#include "libc.h"
#include "init.h"
#include "timer.h"
#include "kheap.h"
#include "device_model.h"

static struct dirent sys_dirent;
// Note: sys_root is dynamically allocated in init_sysfs now
static struct inode sys_kernel;
static struct inode sys_devices;
static struct inode sys_kernel_version;
static struct inode sys_kernel_uptime;
typedef struct sysfs_dev_entry {
    int used;
    struct device *dev;
    struct inode dir;
    struct inode f_type;
    struct inode f_bus;
    struct inode f_driver;
} sysfs_dev_entry_t;
static sysfs_dev_entry_t sys_dev_entries[16];

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
    return NULL;
}

static struct dirent *sys_devices_readdir(struct file *file, uint32_t index)
{
    struct inode *node = file->dentry->inode;
    (void)node;
    struct device *dev = device_model_device_at(index);
    if (!dev)
        return NULL;
    strcpy(sys_dirent.name, dev->kobj.name);
    sys_dirent.ino = 0x6000 + index;
    return &sys_dirent;
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

void init_sysfs(void)
{
    struct inode *sys_root = (struct inode *)kmalloc(sizeof(struct inode));
    if (!sys_root)
        return;

    memset(sys_dev_entries, 0, sizeof(sys_dev_entries));
    memset(sys_root, 0, sizeof(struct inode));
    sys_root->flags = FS_DIRECTORY;
    sys_root->f_ops = &sys_dir_ops;
    sys_root->uid = 0;
    sys_root->gid = 0;
    sys_root->i_mode = 0555;

    memset(&sys_kernel, 0, sizeof(sys_kernel));
    sys_kernel.flags = FS_DIRECTORY;
    sys_kernel.i_ino = 1;
    sys_kernel.f_ops = &sys_kernel_dir_ops;
    sys_kernel.uid = 0;
    sys_kernel.gid = 0;
    sys_kernel.i_mode = 0555;

    memset(&sys_devices, 0, sizeof(sys_devices));
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.i_ino = 2;
    sys_devices.f_ops = &sys_devices_dir_ops;
    sys_devices.uid = 0;
    sys_devices.gid = 0;
    sys_devices.i_mode = 0555;

    memset(&sys_kernel_version, 0, sizeof(sys_kernel_version));
    sys_kernel_version.flags = FS_FILE;
    sys_kernel_version.i_ino = 3;
    sys_kernel_version.i_size = 64;
    sys_kernel_version.f_ops = &sys_read_kernel_version_ops;
    sys_kernel_version.uid = 0;
    sys_kernel_version.gid = 0;
    sys_kernel_version.i_mode = 0444;

    memset(&sys_kernel_uptime, 0, sizeof(sys_kernel_uptime));
    sys_kernel_uptime.flags = FS_FILE;
    sys_kernel_uptime.i_ino = 4;
    sys_kernel_uptime.i_size = 64;
    sys_kernel_uptime.f_ops = &sys_read_kernel_uptime_ops;
    sys_kernel_uptime.uid = 0;
    sys_kernel_uptime.gid = 0;
    sys_kernel_uptime.i_mode = 0444;

    struct dentry *sys_dentry = d_alloc(NULL, "/sys");
    sys_dentry->inode = sys_root;
    vfs_mount_root("/sys", sys_dentry);
}

struct super_block *sysfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)fs_type; (void)flags; (void)dev_name; (void)data;
    return NULL;
}

static struct file_system_type sysfs_fs_type = {
    .name = "sysfs",
    .get_sb = sysfs_get_sb,
    .kill_sb = NULL,
    .next = NULL,
};

static int init_sysfs_fs(void)
{
    register_filesystem(&sysfs_fs_type);
    printf("sysfs filesystem registered.\n");
    return 0;
}
module_init(init_sysfs_fs);
