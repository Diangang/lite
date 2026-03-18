#ifndef DEVFS_H
#define DEVFS_H

#include "fs.h"

fs_node_t *devfs_init(void);
fs_node_t *devfs_get_console(void);

#endif
