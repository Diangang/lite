#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>
#include "fs.h"

int init_ramfs_fs(void);
struct inode *ramfs_create_child(struct inode *dir, const char *name, uint32_t type);

#endif
