#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>
#include "vfs.h"

struct vfs_inode *init_ramfs(void);
struct vfs_inode *ramfs_create_child(struct vfs_inode *dir, const char *name, uint32_t type);

#endif
