#include "file.h"
#include "kheap.h"
#include "libc.h"

file_t *file_open_node(fs_node_t *node, uint32_t flags)
{
    if (!node) return NULL;
    file_t *f = (file_t*)kmalloc(sizeof(file_t));
    if (!f) return NULL;
    f->node = node;
    f->pos = 0;
    f->flags = flags;
    open_fs(node, 1, 1);
    return f;
}

file_t *file_open_path(const char *path, uint32_t flags)
{
    if (!path || !fs_root) return NULL;
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) return NULL;
    return file_open_node(node, flags);
}

uint32_t file_read(file_t *f, uint8_t *buf, uint32_t len)
{
    if (!f || !f->node || !buf || len == 0) return 0;
    uint32_t n = read_fs(f->node, f->pos, len, buf);
    f->pos += n;
    return n;
}

uint32_t file_write(file_t *f, const uint8_t *buf, uint32_t len)
{
    if (!f || !f->node || !buf || len == 0) return 0;
    uint32_t n = write_fs(f->node, f->pos, len, (uint8_t*)buf);
    f->pos += n;
    return n;
}

void file_close(file_t *f)
{
    if (!f) return;
    if (f->node) close_fs(f->node);
    kfree(f);
}
