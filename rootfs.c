#include "rootfs.h"
#include "libc.h"
#include "kheap.h"

typedef struct {
    fs_node_t *initrd;
    fs_node_t *proc;
    fs_node_t *dev;
    struct dirent dirent;
} rootfs_state_t;

static uint32_t rootfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    rootfs_state_t *st = (rootfs_state_t*)(uintptr_t)node->impl;
    if (!st || !st->initrd) return 0;
    return read_fs(st->initrd, offset, size, buffer);
}

static struct dirent *rootfs_readdir(fs_node_t *node, uint32_t index)
{
    rootfs_state_t *st = (rootfs_state_t*)(uintptr_t)node->impl;
    if (!st) return NULL;
    if (index == 0) {
        strcpy(st->dirent.name, "proc");
        st->dirent.ino = st->proc ? st->proc->inode : 0;
        return &st->dirent;
    }
    if (index == 1) {
        strcpy(st->dirent.name, "dev");
        st->dirent.ino = st->dev ? st->dev->inode : 0;
        return &st->dirent;
    }
    if (!st->initrd) return NULL;
    return readdir_fs(st->initrd, index - 2);
}

static fs_node_t *rootfs_finddir(fs_node_t *node, char *name)
{
    rootfs_state_t *st = (rootfs_state_t*)(uintptr_t)node->impl;
    if (!st || !name) return NULL;
    if (!strcmp(name, "proc")) return st->proc;
    if (!strcmp(name, "dev")) return st->dev;
    if (!st->initrd) return NULL;
    return finddir_fs(st->initrd, name);
}

fs_node_t *rootfs_make(fs_node_t *initrd, fs_node_t *proc, fs_node_t *dev)
{
    fs_node_t *root = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    rootfs_state_t *st = (rootfs_state_t*)kmalloc(sizeof(rootfs_state_t));
    if (!root || !st) return initrd;

    memset(root, 0, sizeof(*root));
    memset(st, 0, sizeof(*st));
    st->initrd = initrd;
    st->proc = proc;
    st->dev = dev;

    strcpy(root->name, "/");
    root->flags = FS_DIRECTORY;
    root->readdir = &rootfs_readdir;
    root->finddir = &rootfs_finddir;
    root->read = &rootfs_read;
    root->impl = (uint32_t)(uintptr_t)st;
    return root;
}
