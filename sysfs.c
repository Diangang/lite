#include "sysfs.h"
#include "libc.h"
#include "timer.h"

static struct dirent sys_dirent;
static fs_node_t sys_root;
static fs_node_t sys_kernel;
static fs_node_t sys_devices;
static fs_node_t sys_kernel_version;
static fs_node_t sys_kernel_uptime;
static fs_node_t sys_devices_console;
static fs_node_t sys_devices_initrd;

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

static uint32_t sys_read_devices_console(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static const char *text = "console\n";
    uint32_t n = (uint32_t)strlen(text);
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, text + offset, size);
    return size;
}

static uint32_t sys_read_devices_initrd(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    static const char *text = "initrd\n";
    uint32_t n = (uint32_t)strlen(text);
    if (offset >= n) return 0;
    uint32_t remain = n - offset;
    if (size > remain) size = remain;
    memcpy(buffer, text + offset, size);
    return size;
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
    if (index == 0) {
        strcpy(sys_dirent.name, "console");
        sys_dirent.ino = sys_devices_console.inode;
        return &sys_dirent;
    }
    if (index == 1) {
        strcpy(sys_dirent.name, "initrd");
        sys_dirent.ino = sys_devices_initrd.inode;
        return &sys_dirent;
    }
    return NULL;
}

static fs_node_t *sys_devices_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "console")) return &sys_devices_console;
    if (!strcmp(name, "initrd")) return &sys_devices_initrd;
    return NULL;
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
    memset(&sys_root, 0, sizeof(sys_root));
    strcpy(sys_root.name, "sys");
    sys_root.flags = FS_DIRECTORY;
    sys_root.readdir = &sys_readdir;
    sys_root.finddir = &sys_finddir;

    memset(&sys_kernel, 0, sizeof(sys_kernel));
    strcpy(sys_kernel.name, "kernel");
    sys_kernel.flags = FS_DIRECTORY;
    sys_kernel.inode = 1;
    sys_kernel.readdir = &sys_kernel_readdir;
    sys_kernel.finddir = &sys_kernel_finddir;

    memset(&sys_devices, 0, sizeof(sys_devices));
    strcpy(sys_devices.name, "devices");
    sys_devices.flags = FS_DIRECTORY;
    sys_devices.inode = 2;
    sys_devices.readdir = &sys_devices_readdir;
    sys_devices.finddir = &sys_devices_finddir;

    memset(&sys_kernel_version, 0, sizeof(sys_kernel_version));
    strcpy(sys_kernel_version.name, "version");
    sys_kernel_version.flags = FS_FILE;
    sys_kernel_version.inode = 3;
    sys_kernel_version.length = 64;
    sys_kernel_version.read = &sys_read_kernel_version;

    memset(&sys_kernel_uptime, 0, sizeof(sys_kernel_uptime));
    strcpy(sys_kernel_uptime.name, "uptime");
    sys_kernel_uptime.flags = FS_FILE;
    sys_kernel_uptime.inode = 4;
    sys_kernel_uptime.length = 64;
    sys_kernel_uptime.read = &sys_read_kernel_uptime;

    memset(&sys_devices_console, 0, sizeof(sys_devices_console));
    strcpy(sys_devices_console.name, "console");
    sys_devices_console.flags = FS_FILE;
    sys_devices_console.inode = 5;
    sys_devices_console.length = 32;
    sys_devices_console.read = &sys_read_devices_console;

    memset(&sys_devices_initrd, 0, sizeof(sys_devices_initrd));
    strcpy(sys_devices_initrd.name, "initrd");
    sys_devices_initrd.flags = FS_FILE;
    sys_devices_initrd.inode = 6;
    sys_devices_initrd.length = 32;
    sys_devices_initrd.read = &sys_read_devices_initrd;

    return &sys_root;
}
