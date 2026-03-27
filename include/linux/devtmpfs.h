#ifndef LINUX_DEVTMPFS_H
#define LINUX_DEVTMPFS_H

#include <stdint.h>

struct device;
struct inode;

enum {
    CONSOLE_IOCTL_GETFLAGS = 0x100,
    CONSOLE_IOCTL_SETFLAGS = 0x101
};

enum {
    CONSOLE_TTY_ECHO = 1 << 0,
    CONSOLE_TTY_CANON = 1 << 1
};

struct inode *devtmpfs_get_console(void);
struct inode *devtmpfs_get_tty(void);
void devtmpfs_register_device(struct device *dev);
void devtmpfs_unregister_device(struct device *dev);

#endif
