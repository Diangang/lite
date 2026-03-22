#include "fs.h"
#include "libc.h"

struct dirent *readdir_fs(struct vfs_inode *node, uint32_t index)
{
    if ((node->flags & 0x7) == FS_DIRECTORY && node->f_ops && node->f_ops->readdir != NULL)
        return node->f_ops->readdir(node, index);
    return NULL;
}

struct vfs_inode *finddir_fs(struct vfs_inode *node, const char *name)
{
    if (!node || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || !node->f_ops || node->f_ops->finddir == NULL) return NULL;
    return node->f_ops->finddir(node, name);
}
