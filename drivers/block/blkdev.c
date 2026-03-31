#include "linux/blkdev.h"
#include "linux/vmalloc.h"
#include "linux/libc.h"

static uint32_t blk_reads;
static uint32_t blk_writes;
static uint32_t blk_bytes_read;
static uint32_t blk_bytes_written;

int block_device_init(struct block_device *bdev, uint32_t size, uint32_t block_size)
{
    if (!bdev || size == 0)
        return -1;
    uint8_t *data = (uint8_t *)vmalloc(size);
    if (!data)
        return -1;
    memset(data, 0, size);
    bdev->size = size;
    bdev->block_size = block_size ? block_size : 512;
    bdev->data = data;
    bdev->reads = 0;
    bdev->writes = 0;
    bdev->bytes_read = 0;
    bdev->bytes_written = 0;
    return 0;
}

uint32_t block_device_read(struct block_device *bdev, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (offset + size > bdev->size)
        size = bdev->size - offset;
    memcpy(buffer, bdev->data + offset, size);
    bdev->reads++;
    bdev->bytes_read += size;
    blk_reads++;
    blk_bytes_read += size;
    return size;
}

uint32_t block_device_write(struct block_device *bdev, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!bdev || !buffer || size == 0)
        return 0;
    if (offset >= bdev->size)
        return 0;
    if (offset + size > bdev->size)
        size = bdev->size - offset;
    memcpy(bdev->data + offset, buffer, size);
    bdev->writes++;
    bdev->bytes_written += size;
    blk_writes++;
    blk_bytes_written += size;
    return size;
}

void get_block_stats(uint32_t *reads, uint32_t *writes, uint32_t *bytes_read, uint32_t *bytes_written)
{
    if (reads)
        *reads = blk_reads;
    if (writes)
        *writes = blk_writes;
    if (bytes_read)
        *bytes_read = blk_bytes_read;
    if (bytes_written)
        *bytes_written = blk_bytes_written;
}
