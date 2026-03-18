#ifndef ROOTFS_H
#define ROOTFS_H

#include "fs.h"

fs_node_t *rootfs_make(fs_node_t *initrd, fs_node_t *proc, fs_node_t *dev, fs_node_t *sys);

#endif
