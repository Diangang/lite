#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include "fs.h"

typedef struct vfs_super_block {
    const char *name;
    fs_node_t *root;
    void *fs_private;
} vfs_super_block_t;

typedef struct vfs_inode {
    fs_node_t *node;
    uint32_t mode;
} vfs_inode_t;

typedef struct vfs_dentry {
    const char *name;
    struct vfs_dentry *parent;
    vfs_inode_t *inode;
} vfs_dentry_t;

typedef struct vfs_file {
    vfs_dentry_t *dentry;
    uint32_t pos;
    uint32_t flags;
} vfs_file_t;

typedef struct vfs_mount {
    const char *path;
    vfs_super_block_t *sb;
} vfs_mount_t;

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

void vfs_init(void);
int vfs_mount_root(const char *path, fs_node_t *root_node);
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);
int vfs_mkdir(const char *path);
fs_node_t *vfs_resolve(const char *path);
vfs_file_t *vfs_open(const char *path, uint32_t flags);
vfs_file_t *vfs_open_node(fs_node_t *node, uint32_t flags);
uint32_t vfs_read(vfs_file_t *f, uint8_t *buf, uint32_t len);
uint32_t vfs_write(vfs_file_t *f, const uint8_t *buf, uint32_t len);
int vfs_ioctl(vfs_file_t *f, uint32_t request, uint32_t arg);
void vfs_close(vfs_file_t *f);

#endif
