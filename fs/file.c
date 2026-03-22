#include "file.h"
#include "kheap.h"
#include "libc.h"
#include "fs.h"
#include "ramfs.h"

struct vfs_file *vfs_open(const char *path, uint32_t flags)
{
    struct vfs_dentry *dentry = path_walk(path);
    struct vfs_inode *node = dentry ? dentry->inode : NULL;

    if (!dentry && (flags & VFS_O_CREAT)) {
        char abs[256];
        if (!vfs_build_abs(path, abs, sizeof(abs))) return NULL;
        char parent[256];
        uint32_t abs_len = (uint32_t)strlen(abs);
        if (abs_len < 2) return NULL;
        uint32_t slash = abs_len;
        while (slash > 0 && abs[slash - 1] != '/') slash--;
        if (slash == 0 || slash >= abs_len) return NULL;
        if (slash == 1) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, abs, slash);
            parent[slash] = 0;
        }
        const char *name = abs + slash;
        if (!*name) return NULL;

        struct vfs_dentry *pdentry = path_walk(parent);
        if (!pdentry) return NULL;
        struct vfs_inode *pnode = pdentry->inode;
        if (!pnode || (pnode->flags & 0x7) != FS_DIRECTORY) return NULL;
        if (!vfs_check_access(pnode, 0, 1, 1)) return NULL;

        struct vfs_inode *created = ramfs_create_child(pnode, name, FS_FILE);
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

    struct vfs_file *f = vfs_open_dentry(dentry, flags);
    if (!f) return NULL;
    if ((flags & VFS_O_TRUNC) && (node->flags & 0x7) == FS_FILE) {
        node->length = 0;
    }
    return f;
}

struct vfs_file *vfs_open_dentry(struct vfs_dentry *dentry, uint32_t flags)
{
    if (!dentry || !dentry->inode) return NULL;
    struct vfs_file *f = (struct vfs_file*)kmalloc(sizeof(struct vfs_file));
    if (!f) return NULL;
    dentry->refcount++;
    f->dentry = dentry;
    f->pos = 0;
    f->flags = flags;
    f->refcount = 1;
    open_fs(dentry->inode, 1, 1);
    return f;
}

struct vfs_file *vfs_open_node(struct vfs_inode *node, uint32_t flags)
{
    if (!node) return NULL;
    struct vfs_dentry *d = vfs_dentry_get(node, "");
    if (!d) return NULL;
    struct vfs_file *f = vfs_open_dentry(d, flags);
    vfs_dentry_put(d); // open_dentry incremented it
    return f;
}

uint32_t vfs_read(struct vfs_file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 1, 0, 0)) return 0;
    uint32_t n = read_fs(f->dentry->inode, f->pos, len, buf);
    f->pos += n;
    return n;
}

uint32_t vfs_write(struct vfs_file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->dentry || !f->dentry->inode) return 0;
    if (!vfs_check_access(f->dentry->inode, 0, 1, 0)) return 0;
    uint32_t n = write_fs(f->dentry->inode, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

int vfs_ioctl(struct vfs_file *f, uint32_t request, uint32_t arg)
{
    if (!f || !f->dentry || !f->dentry->inode) return -1;
    return ioctl_fs(f->dentry->inode, request, arg);
}

void vfs_close(struct vfs_file *f)
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

struct file *file_open_node(struct vfs_inode *node, uint32_t flags)
{
    if (!node) return NULL;
    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f) return NULL;
    f->node = node;
    f->flags = flags;
    f->vf = vfs_open_node(node, flags);
    f->refcount = 1;
    if (!f->vf) {
        kfree(f);
        return NULL;
    }
    return f;
}

struct file *file_open_path(const char *path, uint32_t flags)
{
    if (!path) return NULL;
    struct vfs_file *vf = vfs_open(path, flags);
    if (!vf) return NULL;

    struct file *f = (struct file*)kmalloc(sizeof(struct file));
    if (!f) {
        vfs_close(vf);
        return NULL;
    }
    f->node = vf->dentry && vf->dentry->inode ? vf->dentry->inode : NULL;
    f->flags = flags;
    f->vf = vf;
    f->refcount = 1;
    if (!f->node) {
        file_close(f);
        return NULL;
    }
    return f;
}

uint32_t file_read(struct file *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->vf || !buf || len == 0) return 0;
    return vfs_read(f->vf, buf, len);
}

uint32_t file_write(struct file *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->vf || !buf || len == 0) return 0;
    return vfs_write(f->vf, buf, len);
}

int file_ioctl(struct file *f, uint32_t request, uint32_t arg)
{
    if (!f || !f->node) return -1;
    return ioctl_fs(f->node, request, arg);
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
    if (f->refcount > 1) {
        f->refcount--;
        return;
    }
    if (f->vf) vfs_close(f->vf);
    kfree(f);
}
