#ifndef LINUX_CHRDEV_H
#define LINUX_CHRDEV_H

#include <stdint.h>
#include "linux/kdev_t.h"

struct inode;

enum {
    CONSOLE_IOCTL_GETFLAGS = 0x100,
    CONSOLE_IOCTL_SETFLAGS = 0x101
};

enum {
    CONSOLE_TTY_ECHO = 1 << 0,
    CONSOLE_TTY_CANON = 1 << 1
};

struct inode *chrdev_inode_create(dev_t devt, uint32_t mode, uint32_t uid, uint32_t gid);
void chrdev_inode_destroy(struct inode *inode);

#endif
