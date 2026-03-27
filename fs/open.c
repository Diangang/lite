#include "linux/fs.h"
#include "linux/sched.h"
#include "linux/libc.h"

void open_fs(struct inode *node, uint8_t read, uint8_t write)
{
    (void)read;
    (void)write;
    if (node->f_ops && node->f_ops->open != NULL)
        node->f_ops->open(node);
}

void close_fs(struct inode *node)
{
    if (node->f_ops && node->f_ops->close != NULL)
        node->f_ops->close(node);
}

int vfs_chdir(const char *path)
{
    if (!path || !*path)
        return -1;
    struct dentry *d = path_walk(path);
    if (!d)
        return -1;
    struct inode *node = d->inode;
    if (!node)
        return -1;
    if ((node->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(node, 0, 0, 1))
        return -1;
    if (!current)
        return -1;
    if (current->fs.pwd)
        vfs_dentry_put(current->fs.pwd);
    d->refcount++;
    current->fs.pwd = d;
    return 0;
}
