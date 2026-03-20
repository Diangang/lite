#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include "fs.h"

struct vfs_super_block {
    const char *name;
    struct fs_node *root;
    void *fs_private;
    uint32_t refcount;
};

struct vfs_inode {
    struct fs_node *node;
    uint32_t mode;
    uint32_t refcount;
};

struct vfs_dentry {
    const char *name;
    struct vfs_dentry *parent;
    struct vfs_inode *inode;
    uint32_t refcount;
    void *cache;
};

struct vfs_file {
    struct vfs_dentry *dentry;
    uint32_t pos;
    uint32_t flags;
    uint32_t refcount;
};

struct vfs_mount {
    const char *path;
    struct vfs_super_block *sb;
};

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

void vfs_init(void);
int vfs_mount_root(const char *path, struct fs_node *root_node);
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);
int vfs_mkdir(const char *path);
struct fs_node *vfs_resolve(const char *path);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_check_access(struct fs_node *node, int want_read, int want_write, int want_exec);
struct vfs_file *vfs_open(const char *path, uint32_t flags);
struct vfs_file *vfs_open_node(struct fs_node *node, uint32_t flags);
uint32_t vfs_read(struct vfs_file *f, uint8_t *buf, uint32_t len);
uint32_t vfs_write(struct vfs_file *f, const uint8_t *buf, uint32_t len);
int vfs_ioctl(struct vfs_file *f, uint32_t request, uint32_t arg);
void vfs_close(struct vfs_file *f);

#endif
