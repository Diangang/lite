#include "linux/file.h"
#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/fdtable.h"
#include "linux/kheap.h"
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

struct inode *finddir_fs(struct inode *node, const char *name)
{
    if (!node || !name)
        return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY || !node->f_ops || node->f_ops->finddir == NULL)
        return NULL;
    return node->f_ops->finddir(node, name);
}

static struct dirent generic_dirent;

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
