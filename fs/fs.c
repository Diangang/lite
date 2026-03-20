#include "fs.h"
#include "libc.h"

struct fs_node *fs_root = NULL;

uint32_t read_fs(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (node->read != NULL)
        return node->read(node, offset, size, buffer);
    return 0;
}

uint32_t write_fs(struct fs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (node->write != NULL)
        return node->write(node, offset, size, buffer);
    return 0;
}

void open_fs(struct fs_node *node, uint8_t read, uint8_t write)
{
    (void)read;
    (void)write;
    if (node->open != NULL)
        node->open(node);
}

void close_fs(struct fs_node *node)
{
    if (node->close != NULL)
        node->close(node);
}

struct dirent *readdir_fs(struct fs_node *node, uint32_t index)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != NULL)
        return node->readdir(node, index);
    return NULL;
}

struct fs_node *finddir_fs(struct fs_node *node, char *name)
{
    if (!node || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || node->finddir == NULL) return NULL;

    while (*name == '/') name++;
    if (*name == 0) return node;

    char *slash = NULL;
    for (char *p = name; *p; p++) {
        if (*p == '/') {
            slash = p;
            break;
        }
    }
    if (!slash) return node->finddir(node, name);

    char part[128];
    uint32_t n = (uint32_t)(slash - name);
    if (n == 0 || n >= sizeof(part)) return NULL;
    memcpy(part, name, n);
    part[n] = 0;

    struct fs_node *child = node->finddir(node, part);
    if (!child) return NULL;

    while (*slash == '/') slash++;
    if (*slash == 0) return child;
    return finddir_fs(child, slash);
}

int ioctl_fs(struct fs_node *node, uint32_t request, uint32_t arg)
{
    if (!node || node->ioctl == NULL) return -1;
    return node->ioctl(node, request, arg);
}
