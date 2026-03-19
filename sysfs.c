#include "sysfs.h"
#include "libc.h"
#include "timer.h"
#include "device_model.h"

static struct dirent sys_dirent;
static fs_node_t sys_root;
static fs_node_t sys_kernel;
static fs_node_t sys_devices;
static fs_node_t sys_kernel_version;
static fs_node_t sys_kernel_uptime;
typedef struct sysfs_dev_entry {
    int used;
    device_t *dev;
    fs_node_t dir;
    fs_node_t f_type;
    fs_node_t f_bus;
    fs_node_t f_driver;
} sysfs_dev_entry_t;
static sysfs_dev_entry_t sys_dev_entries[16];

static uint32_t sys_read_kernel_version(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static const char *text = "lite-os 0.2\n";
    uint32_t n = (uint32_t)strlen(text);
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, text + offset, size);
    return size;
}

static uint32_t sys_read_kernel_uptime(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
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
    if (offset >= off) return 0;
    uint32_t remain = off - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t sys_read_device_type(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer) return 0;
    device_t *dev = (device_t*)(uintptr_t)node->impl;
    const char *text = (dev && dev->type) ? dev->type : "unknown";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t sys_read_device_bus(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer) return 0;
    device_t *dev = (device_t*)(uintptr_t)node->impl;
    const char *text = (dev && dev->bus) ? dev->bus->kobj.name : "none";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static uint32_t sys_read_device_driver(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer) return 0;
    device_t *dev = (device_t*)(uintptr_t)node->impl;
    const char *text = (dev && dev->driver) ? dev->driver->kobj.name : "unbound";
    char tmp[64];
    uint32_t n = (uint32_t)strlen(text);
    if (n + 1 >= sizeof(tmp)) n = sizeof(tmp) - 2;
    memcpy(tmp, text, n);
    tmp[n++] = '\n';
    tmp[n] = 0;
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static sysfs_dev_entry_t *sysfs_get_device_entry(device_t *dev)
{
    if (!dev) return NULL;
    for (uint32_t i = 0; i < (sizeof(sys_dev_entries) / sizeof(sys_dev_entries[0])); i++) {
        if (sys_dev_entries[i].used && sys_dev_entries[i].dev == dev) return &sys_dev_entries[i];
    }
    for (uint32_t i = 0; i < (sizeof(sys_dev_entries) / sizeof(sys_dev_entries[0])); i++) {
        if (!sys_dev_entries[i].used) {
            sysfs_dev_entry_t *e = &sys_dev_entries[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->dev = dev;

            memset(&e->dir, 0, sizeof(e->dir));
            uint32_t dn = (uint32_t)strlen(dev->kobj.name);
            if (dn >= sizeof(e->dir.name)) dn = sizeof(e->dir.name) - 1;
            memcpy(e->dir.name, dev->kobj.name, dn);
            e->dir.name[dn] = 0;
            e->dir.flags = FS_DIRECTORY;
            e->dir.inode = 0x7000 + i;
            e->dir.readdir = NULL;
            e->dir.finddir = NULL;
            e->dir.impl = (uint32_t)(uintptr_t)e;
            e->dir.uid = 0;
            e->dir.gid = 0;
            e->dir.mask = 0555;

            memset(&e->f_type, 0, sizeof(e->f_type));
            strcpy(e->f_type.name, "type");
            e->f_type.flags = FS_FILE;
            e->f_type.inode = 0x7100 + i;
            e->f_type.length = 64;
            e->f_type.read = &sys_read_device_type;
            e->f_type.impl = (uint32_t)(uintptr_t)dev;
            e->f_type.uid = 0;
            e->f_type.gid = 0;
            e->f_type.mask = 0444;

            memset(&e->f_bus, 0, sizeof(e->f_bus));
            strcpy(e->f_bus.name, "bus");
            e->f_bus.flags = FS_FILE;
            e->f_bus.inode = 0x7200 + i;
            e->f_bus.length = 64;
            e->f_bus.read = &sys_read_device_bus;
            e->f_bus.impl = (uint32_t)(uintptr_t)dev;
            e->f_bus.uid = 0;
            e->f_bus.gid = 0;
            e->f_bus.mask = 0444;

            memset(&e->f_driver, 0, sizeof(e->f_driver));
            strcpy(e->f_driver.name, "driver");
            e->f_driver.flags = FS_FILE;
            e->f_driver.inode = 0x7300 + i;
            e->f_driver.length = 64;
            e->f_driver.read = &sys_read_device_driver;
            e->f_driver.impl = (uint32_t)(uintptr_t)dev;
            e->f_driver.uid = 0;
            e->f_driver.gid = 0;
            e->f_driver.mask = 0444;

            return e;
        }
    }
    return NULL;
}

static struct dirent *sys_devdir_readdir(fs_node_t *node, uint32_t index)
{
    if (!node) return NULL;
    sysfs_dev_entry_t *e = (sysfs_dev_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used) return NULL;
    if (index == 0) {
        strcpy(sys_dirent.name, "type");
        sys_dirent.ino = e->f_type.inode;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "bus");
        sys_dirent.ino = e->f_bus.inode;
        return &sys_dirent;
    }
    if (index == 2) {
        strcpy(sys_dirent.name, "driver");
        sys_dirent.ino = e->f_driver.inode;
        return &sys_dirent;
    }
    return NULL;
}

static fs_node_t *sys_devdir_finddir(fs_node_t *node, char *name)
{
    if (!node || !name) return NULL;
    sysfs_dev_entry_t *e = (sysfs_dev_entry_t*)(uintptr_t)node->impl;
    if (!e || !e->used) return NULL;
    if (!strcmp(name, "type")) return &e->f_type;
    if (!strcmp(name, "bus")) return &e->f_bus;
    if (!strcmp(name, "driver")) return &e->f_driver;
    return NULL;
}

static struct dirent *sys_kernel_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "version");
        sys_dirent.ino = sys_kernel_version.inode;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "uptime");
        sys_dirent.ino = sys_kernel_uptime.inode;
        return &sys_dirent;
    }
    return NULL;
}

static fs_node_t *sys_kernel_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "version")) return &sys_kernel_version;
    if (!strcmp(name, "uptime")) return &sys_kernel_uptime;
    return NULL;
}

static struct dirent *sys_devices_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    device_t *dev = device_model_device_at(index);
    if (!dev) return NULL;
    strcpy(sys_dirent.name, dev->kobj.name);
    sys_dirent.ino = 0x6000 + index;
    return &sys_dirent;
}

static fs_node_t *sys_devices_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    device_t *dev = device_model_find_device(name);
    if (!dev) return NULL;
    sysfs_dev_entry_t *e = sysfs_get_device_entry(dev);
    if (!e) return NULL;
    e->dir.readdir = &sys_devdir_readdir;
    e->dir.finddir = &sys_devdir_finddir;
    return &e->dir;
}

static struct dirent *sys_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(sys_dirent.name, "kernel");
        sys_dirent.ino = sys_kernel.inode;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "devices");
        sys_dirent.ino = sys_devices.inode;
        return &sys_dirent;
    }
    return NULL;
}

static fs_node_t *sys_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "kernel")) return &sys_kernel;
    if (!strcmp(name, "devices")) return &sys_devices;
    return NULL;
}

fs_node_t *sysfs_init(void)
{
    memset(sys_dev_entries, 0, sizeof(sys_dev_entries));
    memset(&sys_root, 0, sizeof(sys_root));
    strcpy(sys_root.name, "sys");
    sys_root.flags = FS_DIRECTORY;
    sys_root.readdir = &sys_readdir;
    sys_root.finddir = &sys_finddir;
    sys_root.uid = 0;
    sys_root.gid = 0;
    sys_root.mask = 0555;

    memset(&sys_kernel, 0, sizeof(sys_kernel));
    strcpy(sys_kernel.name, "kernel");
    sys_kernel.flags = FS_DIRECTORY;
    sys_kernel.inode = 1;
    sys_kernel.readdir = &sys_kernel_readdir;
    sys_kernel.finddir = &sys_kernel_finddir;
    sys_kernel.uid = 0;
    sys_kernel.gid = 0;
    sys_kernel.mask = 0555;

    memset(&sys_devices, 0, sizeof(sys_devices));
    strcpy(sys_devices.name, "devices");
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.inode = 2;
    sys_devices.readdir = &sys_devices_readdir;
    sys_devices.finddir = &sys_devices_finddir;
    sys_devices.uid = 0;
    sys_devices.gid = 0;
    sys_devices.mask = 0555;

    memset(&sys_kernel_version, 0, sizeof(sys_kernel_version));
    strcpy(sys_kernel_version.name, "version");
    sys_kernel_version.flags = FS_FILE;
    sys_kernel_version.inode = 3;
    sys_kernel_version.length = 64;
    sys_kernel_version.read = &sys_read_kernel_version;
    sys_kernel_version.uid = 0;
    sys_kernel_version.gid = 0;
    sys_kernel_version.mask = 0444;

    memset(&sys_kernel_uptime, 0, sizeof(sys_kernel_uptime));
    strcpy(sys_kernel_uptime.name, "uptime");
    sys_kernel_uptime.flags = FS_FILE;
    sys_kernel_uptime.inode = 4;
    sys_kernel_uptime.length = 64;
    sys_kernel_uptime.read = &sys_read_kernel_uptime;
    sys_kernel_uptime.uid = 0;
    sys_kernel_uptime.gid = 0;
    sys_kernel_uptime.mask = 0444;

    return &sys_root;
}
