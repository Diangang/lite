#ifndef LINUX_RAMFS_H
#define LINUX_RAMFS_H

#include <stdint.h>
#include "linux/fs.h"

int init_ramfs_fs(void);
struct inode *ramfs_create_child(struct inode *dir, const char *name, uint32_t type);

#endif
