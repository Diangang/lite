#ifndef LINUX_FS_H
#define LINUX_FS_H

#include <stdint.h>
#include <stddef.h>
#include "linux/kdev_t.h"
#include "linux/path.h"

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08

struct inode;
struct dentry;
struct file;
struct super_block;
struct multiboot_info;

#include "linux/pagemap.h"

struct file_operations {
    uint32_t (*read)(struct inode*, uint32_t, uint32_t, uint8_t*);
    uint32_t (*write)(struct inode*, uint32_t, uint32_t, const uint8_t*);
    void (*open)(struct inode*);
    void (*close)(struct inode*);
    struct dirent * (*readdir)(struct file*, uint32_t);
    int (*ioctl)(struct inode*, uint32_t, uint32_t);
};

struct inode_operations {
    struct inode *(*lookup)(struct inode *dir, const char *name);
    struct inode *(*create)(struct inode *dir, const char *name);
    struct inode *(*mkdir)(struct inode *dir, const char *name);
    struct inode *(*symlink)(struct inode *dir, const char *name, const char *target);
    int (*unlink)(struct dentry *dir_dentry, const char *name);
    int (*rmdir)(struct dentry *dir_dentry, const char *name);
};

struct inode {
    uint32_t i_mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t i_ino;
    uint32_t i_size;
    uintptr_t impl;

    struct address_space *i_mapping;
    struct inode_operations *i_op;
    struct file_operations *f_ops;
    void *i_private;
    void *private_data;

    uint32_t mode;
    uint32_t refcount;
};

struct dirent {
    char name[128];
    uint32_t ino;
};

struct dentry {
    const char *name;
    struct dentry *parent;
    struct dentry *children;
    struct dentry *sibling;
    struct dentry *hash_next;
    struct inode *inode;
    struct vfsmount *mount;
    uint32_t refcount;
    uint32_t d_flags;
    void *cache;
};

/* Minimal dentry flags (Linux mapping: DCACHE_* flags). */
#define DENTRY_NEGATIVE 0x00000001u

struct file_system_type {
    const char *name;
    struct super_block *(*get_sb)(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
    int (*fill_super)(struct super_block *sb, void *data, int silent);
    void (*kill_sb)(struct super_block *sb);
    struct file_system_type *next;
};

struct super_block {
    const char *name;
    struct dentry *s_root;
    void *fs_private;
    uint32_t refcount;
};

struct vfsmount {
    const char *path;
    struct super_block *sb;
    struct dentry *root;
    /*
     * Linux mapping:
     * - vfsmount is linked into a mount topology (mountpoint + parent mount).
     * - Lite keeps a minimal subset to support correct ".." traversal across mounts.
     */
    struct dentry *mountpoint;
    struct vfsmount *parent;
    struct vfsmount *next;
};

extern struct dentry *vfs_root_dentry;

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

void vfs_init(void);

struct super_block *vfs_get_sb_single(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
struct vfsmount *vfs_get_mounts(void);

int register_filesystem(struct file_system_type *fs);
int unregister_filesystem(struct file_system_type *fs);

int vfs_path_is_prefix(const char *path, const char *prefix, uint32_t *out_tail_off);
int vfs_normalize_path(const char *path, char *out, uint32_t cap);
const char *task_get_cwd(void);

int vfs_mount(const char *path, struct super_block *sb);
int vfs_mount_fs(const char *path, const char *fs_name);
int vfs_mount_fs_dev(const char *path, const char *fs_name, const char *dev_name);
int vfs_chdir(const char *path);
struct inode *vfs_resolve_at(struct dentry *root, const char *path);
int vfs_create_path_at(struct dentry *root, const char *path);
int vfs_mknod_at(struct dentry *root, const char *path, uint32_t type, dev_t devt,
                 void *private_data, uint32_t mode, uint32_t uid, uint32_t gid);
int vfs_unlink_at(struct dentry *root, const char *path);
int vfs_rmdir_at(struct dentry *root, const char *path);
int vfs_mkdir(const char *path);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
struct inode *vfs_resolve(const char *path);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_check_access(struct inode *node, int want_read, int want_write, int want_exec);
struct file *vfs_open(const char *path, uint32_t flags);
struct file *vfs_open_dentry(struct dentry *dentry, uint32_t flags);
uint32_t vfs_read(struct file *f, uint8_t *buf, uint32_t len);
uint32_t vfs_write(struct file *f, const uint8_t *buf, uint32_t len);
int vfs_ioctl(struct file *f, uint32_t request, uint32_t arg);
void vfs_close(struct file *f);

uint32_t get_next_ino(void);
void init_special_inode(struct inode *inode, uint32_t type, dev_t devt, struct file_operations *f_ops);
struct inode *alloc_special_inode(uint32_t type, dev_t devt, struct file_operations *f_ops,
                                  uint32_t mode, uint32_t uid, uint32_t gid);
struct inode *create_special_inode(uint32_t type, dev_t devt, void *private_data,
                                   uint32_t mode, uint32_t uid, uint32_t gid);
void destroy_special_inode(struct inode *inode);
int special_inode_matches(struct inode *inode, uint32_t type, dev_t devt);
uint32_t read_fs(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
void open_fs(struct inode *node, uint8_t read, uint8_t write);
void close_fs(struct inode *node);
struct dirent *readdir_fs(struct file *file, uint32_t index);
struct inode *finddir_fs(struct inode *node, const char *name);
struct inode *create_fs(struct inode *dir, const char *name);
struct inode *mkdir_fs(struct inode *dir, const char *name);
struct inode *symlink_fs(struct inode *dir, const char *name, const char *target);
int unlink_fs(struct dentry *dir_dentry, const char *name);
int rmdir_fs(struct dentry *dir_dentry, const char *name);
int ioctl_fs(struct inode *node, uint32_t request, uint32_t arg);

struct dentry *d_alloc(struct dentry *parent, const char *name);
struct dentry *d_lookup(struct dentry *parent, const char *name);
struct dentry *vfs_dentry_get(struct inode *node, const char *name);
void vfs_dentry_put(struct dentry *d);
void vfs_dentry_detach(struct dentry *d);

#endif
