#include "linux/fs.h"
#include "linux/sched.h"
#include "linux/libc.h"
#include "linux/file.h"
#include "linux/fdtable.h"
#include "linux/uaccess.h"

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

int sys_open(const char *pathname, uint32_t flags, int from_user)
{
    char name[128];
    if (from_user) {
        if (strncpy_from_user(name, sizeof(name), pathname) != 0)
            return -1;
    } else {
        if (!pathname)
            return -1;
        strcpy(name, pathname);
    }
    struct file *f = file_open_path(name, flags);
    if (!f)
        return -1;
    return (int)get_unused_fd(f);
}

int sys_close(int fd)
{
    return close_fd(fd);
}

int sys_chdir(const char *path, int from_user)
{
    char tmp[128];
    if (from_user) {
        if (strncpy_from_user(tmp, sizeof(tmp), path) != 0)
            return -1;
    } else {
        if (!path)
            return -1;
        strcpy(tmp, path);
    }
    return vfs_chdir(tmp) == 0 ? 0 : -1;
}

int sys_getcwd(char *buf, uint32_t cap, int from_user)
{
    if (cap == 0)
        return 0;
    const char *cwd = task_get_cwd();
    if (!cwd)
        return -1;

    uint32_t n = (uint32_t)strlen(cwd) + 1;
    if (n > cap)
        n = cap;

    if (from_user) {
        if (copy_to_user(buf, cwd, n) != 0)
            return -1;
    } else {
        memcpy(buf, cwd, n);
    }
    return (int)n;
}
