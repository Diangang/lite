#ifndef LINUX_BLKDEV_H
#define LINUX_BLKDEV_H

#include <stdint.h>

struct inode;

struct block_device {
    uint32_t size;
    uint32_t block_size;
    uint8_t *data;
    uint32_t reads;
    uint32_t writes;
    uint32_t bytes_read;
    uint32_t bytes_written;
};

int block_device_init(struct block_device *bdev, uint32_t size, uint32_t block_size);
uint32_t block_device_read(struct block_device *bdev, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t block_device_write(struct block_device *bdev, uint32_t offset, uint32_t size, const uint8_t *buffer);
void get_block_stats(uint32_t *reads, uint32_t *writes, uint32_t *bytes_read, uint32_t *bytes_written);
struct inode *blockdev_inode_create(struct block_device *bdev);

#endif
