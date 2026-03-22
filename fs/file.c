#include "file.h"
#include "kheap.h"
#include "libc.h"
#include "fs.h"
#include "ramfs.h"

struct file *vfs_open(const char *path, uint32_t flags)
{
    struct dentry *dentry = path_walk(path);
    struct inode *node = dentry ? dentry->inode : NULL;

    if (!dentry && (flags & VFS_O_CREAT)) {
        char tmp[256];
        uint32_t len = (uint32_t)strlen(path);
        if (len >= sizeof(tmp)) return NULL;
        strcpy(tmp, path);

        uint32_t slash = len;
        while (slash > 0 && tmp[slash - 1] != '/') slash--;
        
        char parent[256];
        if (slash == 0) {
            strcpy(parent, ".");
        } else if (slash == 1) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, tmp, slash);
            parent[slash] = 0;
        }
        const char *name = tmp + slash;
        if (!*name) return NULL;

        struct dentry *pdentry = path_walk(parent);
        if (!pdentry) return NULL;
        struct inode *pnode = pdentry->inode;
        if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY) return NULL;
        if (!vfs_check_access(pnode, 0, 1, 1)) return NULL;

        struct inode *created = ramfs_create_child(pnode, name, FS_FILE);
        if (!created) return NULL;

        dentry = d_alloc(pdentry, name);
        if (!dentry) return NULL;
        dentry->inode = created;
        node = created;
    }

    if (!dentry || !node) {
        return NULL;
    }

    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if (!vfs_check_access(node, 1, 0, 1)) {
            return NULL;
        }
    } else {
        int need_write = (flags & VFS_O_TRUNC) ? 1 : 0;
        if (!vfs_check_access(node, 1, need_write, 0)) {
            return NULL;
        }
    }

    struct file *f = vfs_open_dentry(dentry, flags);
    if (!f) return NULL;
    if ((flags & VFS_O_TRUNC) && (node->flags & 0x7) == FS_FILE) {
        node->length = 0;
    }
    return f;
}

struct file *vfs_open_dentry(struct dentry *dentry, uint32_t flags)
{
    if (!dentry || !dentry->inode) return NULL;
    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f) return NULL;
    dentry->refcount++;
    f->dentry = dentry;
    f->pos = 0;
    f->flags = flags;
    f->refcount = 1;
    open_fs(dentry->inode, 1, 1);
    return f;
}

struct file *vfs_open_node(struct inode *node, uint32_t flags)
{
    if (!node) return NULL;
    struct dentry *d = vfs_dentry_get(node, "");
    if (!d) return NULL;
    struct file *f = vfs_open_dentry(d, flags);
    vfs_dentry_put(d); // open_dentry incremented it
    return f;
}

uint32_t vfs_read(struct file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 1, 0, 0)) return 0;
    uint32_t n = read_fs(f->dentry->inode, f->pos, len, buf);
    f->pos += n;
    return n;
}

uint32_t vfs_write(struct file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 0, 1, 0)) return 0;
    uint32_t n = write_fs(f->dentry->inode, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

int vfs_ioctl(struct file *f, uint32_t request, uint32_t arg)
{
    if (!f || !f->dentry || !f->dentry->inode) return -1;
    return ioctl_fs(f->dentry->inode, request, arg);
}

void vfs_close(struct file *f)
{
    if (!f) return;
    if (f->refcount > 1) {
        f->refcount--;
        return;
    }
    if (f->dentry && f->dentry->inode) {
        close_fs(f->dentry->inode);
    }
    if (f->dentry) vfs_dentry_put(f->dentry);
    kfree(f);
}

struct file *file_open_node(struct inode *node, uint32_t flags)
{
    if (!node) return NULL;
    return vfs_open_node(node, flags);
}

struct file *file_open_path(const char *path, uint32_t flags)
{
    if (!path) return NULL;
    return vfs_open(path, flags);
}

uint32_t file_read(struct file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !buf || len == 0) return 0;
    return vfs_read(f, buf, len);
}

uint32_t file_write(struct file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !buf || len == 0) return 0;
    return vfs_write(f, buf, len);
}

int file_ioctl(struct file *f, uint32_t request, uint32_t arg)
{
    if (!f) return -1;
    return vfs_ioctl(f, request, arg);
}

struct file *file_dup(struct file *f)
{
    if (!f) return NULL;
    f->refcount++;
    return f;
}

void file_close(struct file *f)
{
    if (!f) return;
    vfs_close(f);
}
