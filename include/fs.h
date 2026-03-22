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

struct vfs_inode;
struct vfs_dentry;
struct vfs_file;
struct vfs_super_block;

/* File Operations Table */
struct vfs_file_operations {
    uint32_t (*read)(struct vfs_inode*, uint32_t, uint32_t, uint8_t*);
    uint32_t (*write)(struct vfs_inode*, uint32_t, uint32_t, const uint8_t*);
    void (*open)(struct vfs_inode*);
    void (*close)(struct vfs_inode*);
    struct dirent * (*readdir)(struct vfs_inode*, uint32_t);
    struct vfs_inode * (*finddir)(struct vfs_inode*, const char *name);
    int (*ioctl)(struct vfs_inode*, uint32_t, uint32_t);
};

/* VFS Inode - Represents a file entity */
struct vfs_inode {
    uint32_t mask;      /* Permissions */
    uint32_t uid;       /* User ID */
    uint32_t gid;       /* Group ID */
    uint32_t flags;     /* Node type (File, Directory, etc.) */
    uint32_t inode;     /* Inode number */
    uint32_t length;    /* Size of the file */
    uint32_t impl;      /* Implementation defined number */

    struct vfs_file_operations *f_ops; /* Pointer to operations */

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
struct vfs_dentry {
    const char *name;
    struct vfs_dentry *parent;
    struct vfs_dentry *children;
    struct vfs_dentry *sibling;
    struct vfs_inode *inode;
    struct vfs_mount *mount;
    uint32_t refcount;
    void *cache;
};

/* VFS File - Represents an open file descriptor */
struct vfs_file {
    struct vfs_dentry *dentry;
    uint32_t pos;
    uint32_t flags;
    uint32_t refcount;
};

/* VFS Superblock - Represents a mounted filesystem */
struct vfs_super_block {
    const char *name;
    struct vfs_inode *root;
    void *fs_private;
    uint32_t refcount;
};

struct vfs_mount {
    const char *path;
    struct vfs_super_block *sb;
    struct vfs_dentry *root;
};

/* Internal VFS state */
extern struct vfs_dentry *vfs_root_dentry;

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

/* Forward declaration for multiboot info */
struct multiboot_info;

/* Standard VFS API */
int vfs_path_is_prefix(const char *path, const char *prefix, uint32_t *out_tail_off);
int vfs_normalize_path(const char *path, char *out, uint32_t cap);
int vfs_build_abs(const char *path, char *abs, uint32_t cap);
const char *vfs_getcwd(void);

int vfs_mount_root(const char *path, struct vfs_inode *root_node);
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);
int vfs_mkdir(const char *path);
struct vfs_inode *vfs_resolve(const char *path);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_check_access(struct vfs_inode *node, int want_read, int want_write, int want_exec);
struct vfs_file *vfs_open(const char *path, uint32_t flags);
struct vfs_file *vfs_open_dentry(struct vfs_dentry *dentry, uint32_t flags);
uint32_t vfs_read(struct vfs_file *f, uint8_t *buf, uint32_t len);
uint32_t vfs_write(struct vfs_file *f, const uint8_t *buf, uint32_t len);
int vfs_ioctl(struct vfs_file *f, uint32_t request, uint32_t arg);
void vfs_close(struct vfs_file *f);

/* Internal operation wrappers */
uint32_t read_fs(struct vfs_inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(struct vfs_inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
void open_fs(struct vfs_inode *node, uint8_t read, uint8_t write);
void close_fs(struct vfs_inode *node);
struct dirent *readdir_fs(struct vfs_inode *node, uint32_t index);
struct vfs_inode *finddir_fs(struct vfs_inode *node, const char *name);
int vfs_check_access(struct vfs_inode *node, int want_read, int want_write, int want_exec);
int vfs_chmod(const char *path, uint32_t mode);

int ioctl_fs(struct vfs_inode *node, uint32_t request, uint32_t arg);

struct vfs_dentry *d_alloc(struct vfs_dentry *parent, const char *name);
struct vfs_dentry *d_lookup(struct vfs_dentry *parent, const char *name);
struct vfs_dentry *path_walk(const char *path);

struct vfs_dentry *vfs_dentry_get(struct vfs_inode *node, const char *name);
void vfs_dentry_put(struct vfs_dentry *d);

#endif
