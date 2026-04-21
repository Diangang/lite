#include "linux/fs.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/file.h"
#include "linux/fdtable.h"

/* ioctl_fs: Implement ioctl fs. */
int ioctl_fs(struct inode *node, uint32_t request, uint32_t arg)
{
    if (!node || !node->f_ops || node->f_ops->ioctl == NULL)
        return -1;
    return node->f_ops->ioctl(node, request, arg);
}

/* sys_ioctl: Implement sys ioctl. */
int sys_ioctl(int fd, uint32_t request, uint32_t arg)
{
    struct file *f = fget(fd);
    if (!f || !f->dentry || !f->dentry->inode)
        return -1;
    return file_ioctl(f, request, arg);
}
