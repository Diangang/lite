#include "devfs.h"
#include "libc.h"
#include "kernel.h"
#include "shell.h"

static struct dirent dev_dirent;
static fs_node_t dev_root;
static fs_node_t dev_console;

static uint32_t dev_console_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;
    (void)offset;
    if (!buffer || size == 0) return 0;
    return shell_tty_read_blocking((char*)buffer, size);
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
        return (int)shell_tty_get_flags();
    }
    if (request == CONSOLE_IOCTL_SETFLAGS) {
        shell_tty_set_flags(arg);
        return 0;
    }
    return -1;
}

static struct dirent *dev_readdir(fs_node_t *node, uint32_t index)
{
    (void)node;
    if (index == 0) {
        strcpy(dev_dirent.name, "console");
        dev_dirent.ino = dev_console.inode;
        return &dev_dirent;
    }
    return NULL;
}

static fs_node_t *dev_finddir(fs_node_t *node, char *name)
{
    (void)node;
    if (!name) return NULL;
    if (!strcmp(name, "console")) return &dev_console;
    return NULL;
}

fs_node_t *devfs_init(void)
{
    memset(&dev_root, 0, sizeof(dev_root));
    strcpy(dev_root.name, "dev");
    dev_root.flags = FS_DIRECTORY;
    dev_root.readdir = &dev_readdir;
    dev_root.finddir = &dev_finddir;

    memset(&dev_console, 0, sizeof(dev_console));
    strcpy(dev_console.name, "console");
    dev_console.flags = FS_CHARDEVICE;
    dev_console.inode = 1;
    dev_console.length = 0;
    dev_console.read = &dev_console_read;
    dev_console.write = &dev_console_write;
    dev_console.ioctl = &dev_console_ioctl;

    return &dev_root;
}

fs_node_t *devfs_get_console(void)
{
    return &dev_console;
}
