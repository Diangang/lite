#include "fs.h"
#include "libc.h"

int ioctl_fs(struct inode *node, uint32_t request, uint32_t arg)
{
    if (!node || !node->f_ops || node->f_ops->ioctl == NULL)
        return -1;
    return node->f_ops->ioctl(node, request, arg);
}
