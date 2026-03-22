#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

enum {
    CONSOLE_IOCTL_GETFLAGS = 0x100,
    CONSOLE_IOCTL_SETFLAGS = 0x101
};

enum {
    CONSOLE_TTY_ECHO = 1 << 0,
    CONSOLE_TTY_CANON = 1 << 1
};

struct vfs_inode *init_devfs(void);
struct vfs_inode *devfs_get_console(void);

#endif
