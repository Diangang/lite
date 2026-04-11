#include "linux/file.h"
#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/fdtable.h"
#include "linux/slab.h"
#include "linux/uaccess.h"

// Generic readdir based on dcache
// Notice: dentry is not directly attached to inode for generic reverse mapping in simple design,
// but we can pass dentry or use a global search if we have to.
// Wait, if we want generic_readdir to iterate dentry->children, we need the dentry!
// Currently readdir_fs takes vfs_inode. We need to modify readdir_fs to take vfs_file which has file->dentry!

struct dirent *readdir_fs(struct file *file, uint32_t index)
{
    if (!file || !file->dentry || !file->dentry->inode)
        return NULL;
    struct inode *node = file->dentry->inode;
    if ((node->flags & 0x7) == FS_DIRECTORY && node->f_ops && node->f_ops->readdir != NULL)
        return node->f_ops->readdir(file, index);
    return NULL;
}

struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[];
} __attribute__((packed));

/* sys_getdents: Implement sys getdents. */
int sys_getdents(int fd, void *dirp, uint32_t count, int from_user)
{
    struct file *f = fget(fd);
    if (!f || !f->dentry || !f->dentry->inode)
        return -1;
    struct inode *node = f->dentry->inode;
    if ((node->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (!vfs_check_access(node, 1, 0, 1))
        return -1;
    if (count < 16)
        return -1;

    char *out = (char*)kmalloc(count);
    if (!out)
        return -1;

    uint32_t off = 0;
    while (off + 12 <= count) {
        struct dirent *de = readdir_fs(f, f->pos);
        if (!de)
            break;
        uint32_t name_len = (uint32_t)strlen(de->name);
        uint32_t reclen = 10 + name_len + 1;
        reclen = (reclen + 3) & ~3;
        if (off + reclen > count) {
            if (off == 0) {
                kfree(out);
                return -1;
            }
            break;
        }

        struct linux_dirent *lde = (struct linux_dirent*)(out + off);
        memset(lde, 0, reclen);
        lde->d_ino = de->ino;
        lde->d_off = f->pos + 1;
        lde->d_reclen = (uint16_t)reclen;
        memcpy(lde->d_name, de->name, name_len);
        lde->d_name[name_len] = 0;

        off += reclen;
        f->pos++;
    }

    if (off == 0) {
        kfree(out);
        return 0;
    }

    if (from_user) {
        if (copy_to_user(dirp, out, off) != 0) {
            kfree(out);
            return -1;
        }
    } else {
        memcpy(dirp, out, off);
    }
    kfree(out);
    return (int)off;
}

/* finddir_fs: Implement finddir fs. */
struct inode *finddir_fs(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    if (node->i_op && node->i_op->lookup)
        return node->i_op->lookup(node, name);
    return NULL;
}

struct inode *create_fs(struct inode *dir, const char *name)
{
    if (!dir || !name)
        return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    if (dir->i_op && dir->i_op->create)
        return dir->i_op->create(dir, name);
    return NULL;
}

struct inode *mkdir_fs(struct inode *dir, const char *name)
{
    if (!dir || !name)
        return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    if (dir->i_op && dir->i_op->mkdir)
        return dir->i_op->mkdir(dir, name);
    return NULL;
}

int unlink_fs(struct dentry *dir_dentry, const char *name)
{
    if (!dir_dentry || !dir_dentry->inode || !name)
        return -1;
    struct inode *dir = dir_dentry->inode;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (dir->i_op && dir->i_op->unlink)
        return dir->i_op->unlink(dir_dentry, name);
    return -1;
}

int rmdir_fs(struct dentry *dir_dentry, const char *name)
{
    if (!dir_dentry || !dir_dentry->inode || !name)
        return -1;
    struct inode *dir = dir_dentry->inode;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return -1;
    if (dir->i_op && dir->i_op->rmdir)
        return dir->i_op->rmdir(dir_dentry, name);
    return -1;
}



/* generic_readdir: Implement generic readdir. */
static struct dirent generic_dirent;

static struct dentry *find_dentry_by_inode(struct dentry *root, struct inode *node)
{
    if (!root || !node)
        return NULL;
    if (root->inode == node)
        return root;
    struct dentry *child = root->children;
    while (child) {
        struct dentry *found = find_dentry_by_inode(child, node);
        if (found)
            return found;
        child = child->sibling;
    }
    return NULL;
}

struct dirent *generic_readdir(struct file *file, uint32_t index)
{
    if (!file || !file->dentry || !file->dentry->inode)
        return NULL;
    struct dentry *d = file->dentry;

    if (index == 0) {
        strcpy(generic_dirent.name, ".");
        generic_dirent.ino = d->inode->i_ino;
        return &generic_dirent;
    }
    if (index == 1) {
        strcpy(generic_dirent.name, "..");
        generic_dirent.ino = d->parent ? d->parent->inode->i_ino : d->inode->i_ino;
        return &generic_dirent;
    }

    index -= 2;
    struct dentry *child = d->children;
    while (child) {
        if (child->inode && child->inode->flags != 0) {
            if (index == 0) {
                strcpy(generic_dirent.name, child->name);
                generic_dirent.ino = child->inode ? child->inode->i_ino : 0;
                return &generic_dirent;
            }
            index--;
        }
        child = child->sibling;
    }

    return NULL;
}

struct inode *generic_finddir(struct inode *node, const char *name)
{
    if (!node || !name || !*name)
        return NULL;
    struct dentry *host = find_dentry_by_inode(vfs_root_dentry, node);
    if (!host)
        return NULL;
    struct dentry *child = d_lookup(host, name);
    if (!child)
        return NULL;
    return child->inode;
}
