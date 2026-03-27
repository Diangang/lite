#ifndef LINUX_DEVFS_H
#define LINUX_DEVFS_H

#include "linux/fs.h"

enum {
    CONSOLE_IOCTL_GETFLAGS = 0x100,
    CONSOLE_IOCTL_SETFLAGS = 0x101
};

enum {
    CONSOLE_TTY_ECHO = 1 << 0,
    CONSOLE_TTY_CANON = 1 << 1
};

void init_devfs(void);
struct inode *devfs_get_console(void);

#endif
