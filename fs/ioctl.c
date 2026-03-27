#include "linux/fs.h"
#include "linux/libc.h"
#include "linux/file.h"
#include "linux/fdtable.h"

int ioctl_fs(struct inode *node, uint32_t request, uint32_t arg)
{
    if (!node || !node->f_ops || node->f_ops->ioctl == NULL)
        return -1;
    return node->f_ops->ioctl(node, request, arg);
}

int sys_ioctl(int fd, uint32_t request, uint32_t arg)
{
    struct file *f = fget(fd);
    if (!f || !f->dentry || !f->dentry->inode)
        return -1;
    return file_ioctl(f, request, arg);
}
