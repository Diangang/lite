#include "file.h"
#include "kheap.h"
#include "libc.h"
#include "vfs.h"

file_t *file_open_node(fs_node_t *node, uint32_t flags)
{
    if (!node) return NULL;
    file_t *f = (file_t*)kmalloc(sizeof(file_t));
    if (!f) return NULL;
    f->node = node;
    f->flags = flags;
    f->vf = vfs_open_node(node, flags);
    if (!f->vf) {
        kfree(f);
        return NULL;
    }
    return f;
}

file_t *file_open_path(const char *path, uint32_t flags)
{
    if (!path) return NULL;
    vfs_file_t *vf = vfs_open(path, flags);
    if (!vf) return NULL;

    file_t *f = (file_t*)kmalloc(sizeof(file_t));
    if (!f) {
        vfs_close(vf);
        return NULL;
    }
    f->node = vf->dentry && vf->dentry->inode ? vf->dentry->inode->node : NULL;
    f->flags = flags;
    f->vf = vf;
    if (!f->node) {
        file_close(f);
        return NULL;
    }
    return f;
}

uint32_t file_read(file_t *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->vf || !buf || len == 0) return 0;
    return vfs_read(f->vf, buf, len);
}

uint32_t file_write(file_t *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->vf || !buf || len == 0) return 0;
    return vfs_write(f->vf, buf, len);
}

void file_close(file_t *f)
{
    if (!f) return;
    if (f->vf) vfs_close(f->vf);
    kfree(f);
}
