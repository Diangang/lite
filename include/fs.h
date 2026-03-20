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

struct fs_node;

/* Callback function types */
typedef uint32_t (*read_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint32_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef int (*ioctl_type_t)(struct fs_node*, uint32_t, uint32_t);

/* File System Node (Inode) */
struct fs_node {
    char name[128];     /* Filename */
    uint32_t mask;      /* Permissions */
    uint32_t uid;       /* User ID */
    uint32_t gid;       /* Group ID */
    uint32_t flags;     /* Node type (File, Directory, etc.) */
    uint32_t inode;     /* Inode number */
    uint32_t length;    /* Size of the file */
    uint32_t impl;      /* Implementation defined number */

    /* Function pointers */
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    ioctl_type_t ioctl;

    struct fs_node *ptr; /* Used for mount points and symlinks */
};

/* Directory Entry */
struct dirent {
    char name[128];
    uint32_t ino;
};

/* Standard FS functions */
uint32_t read_fs(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_fs(struct fs_node *node, uint8_t read, uint8_t write);
void close_fs(struct fs_node *node);
struct dirent *readdir_fs(struct fs_node *node, uint32_t index);
struct fs_node *finddir_fs(struct fs_node *node, char *name);
int ioctl_fs(struct fs_node *node, uint32_t request, uint32_t arg);

/* Root of the filesystem */
extern struct fs_node *fs_root;

#endif
