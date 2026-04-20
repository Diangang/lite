#ifndef LINUX_RAMFS_H
#define LINUX_RAMFS_H

#include <stdint.h>
#include "linux/fs.h"

int init_ramfs_fs(void);
int ramfs_fill_super(struct super_block *sb, void *data, int silent);

#endif
