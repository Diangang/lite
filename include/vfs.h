#ifndef VFS_H
#define VFS_H

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
    struct vfs_inode *inode;
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
};

enum {
    VFS_O_CREAT = 1 << 6,
    VFS_O_TRUNC = 1 << 9
};

/* Forward declaration for multiboot info */
struct multiboot_info;

/* Standard VFS API */
void init_fs(struct multiboot_info* mbi);
void vfs_init(void);
int vfs_mount_root(const char *path, struct vfs_inode *root_node);
int vfs_chdir(const char *path);
const char *vfs_getcwd(void);
int vfs_mkdir(const char *path);
struct vfs_inode *vfs_resolve(const char *path);
int vfs_chmod(const char *path, uint32_t mode);
int vfs_check_access(struct vfs_inode *node, int want_read, int want_write, int want_exec);
struct vfs_file *vfs_open(const char *path, uint32_t flags);
struct vfs_file *vfs_open_node(struct vfs_inode *node, uint32_t flags);
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
int ioctl_fs(struct vfs_inode *node, uint32_t request, uint32_t arg);

#endif
