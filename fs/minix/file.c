// file.c - Lite minix file (Linux mapping: fs/minix/file.c)

#include "minix.h"

static uint32_t minix_file_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    if ((node->flags & 0x7) != FS_FILE)
        return 0;
    struct minix_inode_info *info = (struct minix_inode_info *)node->private_data;
    if (!info || !info->bdev)
        return 0;
    if (offset >= info->dinode.i_size)
        return 0;
    uint32_t remain = info->dinode.i_size - offset;
    if (size > remain)
        size = remain;

    uint32_t done = 0;
    uint32_t zsize = minix_zone_size(&info->sb);
    if (zsize == 0)
        return 0;
    uint8_t blk[MINIX_BLOCK_SIZE];

    while (done < size) {
        uint32_t pos = offset + done;
        uint16_t zone = minix_zone_for_pos(&info->dinode, &info->sb, pos);
        if (!zone)
            break;
        uint32_t zone_off = pos % zsize;
        uint32_t chunk = size - done;
        uint32_t left_in_zone = zsize - zone_off;
        if (chunk > left_in_zone)
            chunk = left_in_zone;

        uint32_t base_block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
        uint32_t block_off = zone_off % MINIX_BLOCK_SIZE;
        uint32_t left_in_block = MINIX_BLOCK_SIZE - block_off;
        uint32_t sub = chunk;
        if (sub > left_in_block)
            sub = left_in_block;

        if (minix_bread(info->bdev, base_block, blk) != MINIX_BLOCK_SIZE)
            break;
        memcpy(buffer + done, blk + block_off, sub);
        done += sub;
    }
    return done;
}

static uint32_t minix_file_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    if ((node->flags & 0x7) != FS_FILE)
        return 0;
    struct minix_inode_info *info = (struct minix_inode_info *)node->private_data;
    if (!info || !info->bdev)
        return 0;

    uint32_t done = 0;
    uint32_t zsize = minix_zone_size(&info->sb);
    if (zsize == 0)
        return 0;
    uint8_t blk[MINIX_BLOCK_SIZE];

    while (done < size) {
        uint32_t pos = offset + done;
        uint16_t zone = minix_zone_for_pos(&info->dinode, &info->sb, pos);
        if (!zone)
            break;
        uint32_t zone_off = pos % zsize;
        uint32_t chunk = size - done;
        uint32_t left_in_zone = zsize - zone_off;
        if (chunk > left_in_zone)
            chunk = left_in_zone;

        uint32_t base_block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
        uint32_t block_off = zone_off % MINIX_BLOCK_SIZE;
        uint32_t left_in_block = MINIX_BLOCK_SIZE - block_off;
        uint32_t sub = chunk;
        if (sub > left_in_block)
            sub = left_in_block;

        if (minix_bread(info->bdev, base_block, blk) != MINIX_BLOCK_SIZE)
            break;
        memcpy(blk + block_off, buffer + done, sub);
        if (minix_bwrite(info->bdev, base_block, blk) != MINIX_BLOCK_SIZE)
            break;
        done += sub;
    }

    if (offset + done > info->dinode.i_size) {
        info->dinode.i_size = offset + done;
        node->i_size = info->dinode.i_size;

        minix_write_inode(info->bdev, &info->sb, info->ino, &info->dinode);
    }

    return done;
}


struct file_operations minix_file_ops = {
    .read = minix_file_read,
    .write = minix_file_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};
