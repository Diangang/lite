#include "file.h"
#include "fs.h"
#include "file.h"
#include "libc.h"

// Generic readdir based on dcache
// Notice: dentry is not directly attached to inode for generic reverse mapping in simple design,
// but we can pass dentry or use a global search if we have to.
// Wait, if we want generic_readdir to iterate dentry->children, we need the dentry!
// Currently readdir_fs takes vfs_inode. We need to modify readdir_fs to take vfs_file which has file->dentry!

struct dirent *readdir_fs(struct file *file, uint32_t index)
{
    if (!file || !file->dentry || !file->dentry->inode) return NULL;
    struct inode *node = file->dentry->inode;
    if ((node->flags & 0x7) == FS_DIRECTORY && node->f_ops && node->f_ops->readdir != NULL)
        return node->f_ops->readdir(file, index);
    return NULL;
}

struct inode *finddir_fs(struct inode *node, const char *name)
{
    if (!node || !name) return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || !node->f_ops || node->f_ops->finddir == NULL) return NULL;
    return node->f_ops->finddir(node, name);
}

static struct dirent generic_dirent;

struct dirent *generic_readdir(struct file *file, uint32_t index)
{
    if (!file || !file->dentry || !file->dentry->inode) return NULL;
    struct dentry *d = file->dentry;

    if (index == 0) {
        strcpy(generic_dirent.name, ".");
        generic_dirent.ino = d->inode->inode;
        return &generic_dirent;
    }
    if (index == 1) {
        strcpy(generic_dirent.name, "..");
        generic_dirent.ino = d->parent ? d->parent->inode->inode : d->inode->inode;
        return &generic_dirent;
    }

    index -= 2;
    struct dentry *child = d->children;
    while (child && index > 0) {
        child = child->sibling;
        index--;
    }

    if (!child) return NULL;

    strcpy(generic_dirent.name, child->name);
    generic_dirent.ino = child->inode ? child->inode->inode : 0;
    return &generic_dirent;
}
