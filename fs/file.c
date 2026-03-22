#include "file.h"
#include "kheap.h"
#include "libc.h"
#include "vfs.h"

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
