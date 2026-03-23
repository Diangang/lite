#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

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
struct vfs_super_block;

#include "pagemap.h"

/* File Operations Table */
struct file_operations {
    uint32_t (*read)(struct inode*, uint32_t, uint32_t, uint8_t*);
    uint32_t (*write)(struct inode*, uint32_t, uint32_t, const uint8_t*);
    void (*open)(struct inode*);
    void (*close)(struct inode*);
    struct dirent * (*readdir)(struct file*, uint32_t);
    struct inode * (*finddir)(struct inode*, const char *name);
    int (*ioctl)(struct inode*, uint32_t, uint32_t);
    int (*unlink)(struct dentry *dir_dentry, const char *name);
};

/* VFS Inode - Represents a file entity */
struct inode {
    uint32_t i_mode;    /* Permissions/Mode */
    uint32_t uid;       /* User ID */
    uint32_t gid;       /* Group ID */
    uint32_t flags;     /* Node type (File, Directory, etc.) */
    uint32_t i_ino;     /* Inode number */
    uint32_t i_size;    /* Size of the file */
    uint32_t impl;      /* Implementation defined number */

    struct address_space *i_mapping; /* Page cache mapping */

    struct file_operations *f_ops; /* Pointer to operations */

    void *private_data; /* Filesystem specific data (was ptr) */

    uint32_t mode;      /* VFS specific mode */
    uint32_t refcount;  /* Reference count */
};

/* Directory Entry */
struct dirent {
    char name[128];
    uint32_t ino;
};

/* VFS Dentry - Represents a name in a directory */
struct dentry {
    const char *name;
    struct dentry *parent;
    struct dentry *children;
    struct dentry *sibling;
    struct inode *inode;
    struct vfs_mount *mount;
    uint32_t refcount;
    void *cache;
};

/* File - Represents an open file descriptor */

/* VFS Superblock - Represents a mounted filesystem */
struct vfs_super_block {
    const char *name;
    struct inode *root;
    void *fs_private;
    uint32_t refcount;
};

struct vfs_mount {
    const char *path;
    struct vfs_super_block *sb;
    struct dentry *root;
};

/* Internal VFS state */
extern struct dentry *vfs_root_dentry;

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

/* Forward declaration for multiboot info */
struct multiboot_info;

/* Standard VFS API */
int vfs_path_is_prefix(const char *path, const char *prefix, uint32_t *out_tail_off);
int vfs_normalize_path(const char *path, char *out, uint32_t cap);
const char *task_get_cwd(void);

int vfs_mount_root(const char *path, struct inode *root_node);
int vfs_chdir(const char *path);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
struct inode *vfs_resolve(const char *path);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_check_access(struct inode *node, int want_read, int want_write, int want_exec);
struct file *vfs_open(const char *path, uint32_t flags);
struct file *vfs_open_dentry(struct dentry *dentry, uint32_t flags);
uint32_t vfs_read(struct file *f, uint8_t *buf, uint32_t len);
uint32_t vfs_write(struct file *f, const uint8_t *buf, uint32_t len);
int vfs_ioctl(struct file *f, uint32_t request, uint32_t arg);
void vfs_close(struct file *f);

/* Internal operation wrappers */
uint32_t get_next_ino(void);
uint32_t read_fs(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
void open_fs(struct inode *node, uint8_t read, uint8_t write);
void close_fs(struct inode *node);
struct dirent *readdir_fs(struct file *file, uint32_t index);
struct inode *finddir_fs(struct inode *node, const char *name);
int vfs_check_access(struct inode *node, int want_read, int want_write, int want_exec);
int vfs_chmod(const char *path, uint32_t mode);

int ioctl_fs(struct inode *node, uint32_t request, uint32_t arg);

struct dentry *d_alloc(struct dentry *parent, const char *name);
struct dentry *d_lookup(struct dentry *parent, const char *name);
struct dentry *path_walk(const char *path);

struct dentry *vfs_dentry_get(struct inode *node, const char *name);
void vfs_dentry_put(struct dentry *d);

#endif
