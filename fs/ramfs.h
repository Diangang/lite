#ifndef RAMFS_H
#define RAMFS_H

#include <stdint.h>
#include "fs.h"

fs_node_t *ramfs_init(void);
fs_node_t *ramfs_create_child(fs_node_t *dir, const char *name, uint32_t type);

#endif
