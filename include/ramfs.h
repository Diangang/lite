#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>
#include "fs.h"

struct fs_node *ramfs_init(void);
struct fs_node *ramfs_create_child(struct fs_node *dir, const char *name, uint32_t type);

#endif
