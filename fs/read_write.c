#include "linux/fs.h"

uint32_t read_fs(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->read != NULL)
        return node->f_ops->read(node, offset, size, buffer);
    return 0;
}

uint32_t write_fs(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->write != NULL)
        return node->f_ops->write(node, offset, size, buffer);
    return 0;
}
