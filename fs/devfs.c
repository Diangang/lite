#include "devfs.h"
#include "libc.h"
#include "kernel.h"
#include "tty.h"

static struct dirent dev_dirent;
static fs_node_t dev_root;
static fs_node_t dev_console;
static fs_node_t dev_tty;

static uint32_t dev_console_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;
    return tty_read_blocking((char*)buffer, size);
}

static uint32_t dev_console_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;
    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar((char)buffer[i]);
    }
    return size;
}

static int dev_console_ioctl(fs_node_t *node, uint32_t request, uint32_t arg)
{
    (void)node;
    if (request == CONSOLE_IOCTL_GETFLAGS) {
        return (int)tty_get_flags();
    }
    if (request == CONSOLE_IOCTL_SETFLAGS) {
        tty_set_flags(arg);
        return 0;
    }
    return -1;
}

static uint32_t dev_tty_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;
    return tty_read_blocking((char*)buffer, size);
}

static uint32_t dev_tty_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;
    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar((char)buffer[i]);
    }
    return size;
}

static int dev_tty_ioctl(fs_node_t *node, uint32_t request, uint32_t arg)
{
    return dev_console_ioctl(node, request, arg);
}

static struct dirent *dev_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(dev_dirent.name, "console");
        dev_dirent.ino = dev_console.inode;
        return &dev_dirent;
    }
    if (index == 1) {
        strcpy(dev_dirent.name, "tty");
        dev_dirent.ino = dev_tty.inode;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *dev_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "console")) return &dev_console;
    if (!strcmp(name, "tty")) return &dev_tty;
    return NULL;
}

fs_node_t *devfs_init(void)
{
    memset(&dev_root, 0, sizeof(dev_root));
    strcpy(dev_root.name, "dev");
    dev_root.flags = FS_DIRECTORY;
    dev_root.readdir = &dev_readdir;
    dev_root.finddir = &dev_finddir;
    dev_root.uid = 0;
    dev_root.gid = 0;
    dev_root.mask = 0555;

    memset(&dev_console, 0, sizeof(dev_console));
    strcpy(dev_console.name, "console");
    dev_console.flags = FS_CHARDEVICE;
    dev_console.inode = 1;
    dev_console.length = 0;
    dev_console.read = &dev_console_read;
    dev_console.write = &dev_console_write;
    dev_console.ioctl = &dev_console_ioctl;
    dev_console.uid = 0;
    dev_console.gid = 0;
    dev_console.mask = 0666;

    memset(&dev_tty, 0, sizeof(dev_tty));
    strcpy(dev_tty.name, "tty");
    dev_tty.flags = FS_CHARDEVICE;
    dev_tty.inode = 2;
    dev_tty.length = 0;
    dev_tty.read = &dev_tty_read;
    dev_tty.write = &dev_tty_write;
    dev_tty.ioctl = &dev_tty_ioctl;
    dev_tty.uid = 0;
    dev_tty.gid = 0;
    dev_tty.mask = 0666;

    return &dev_root;
}

fs_node_t *devfs_get_console(void)
{
    return &dev_console;
}
