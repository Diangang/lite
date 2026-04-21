#ifndef LINUX_BLKDEV_H
#define LINUX_BLKDEV_H

#include <stdint.h>

/*
 * Linux mapping: include/linux/blkdev.h defines BLKDEV_MIN_RQ and enforces it
 * via sysfs queue/nr_requests and queue update helpers.
 */
#define BLKDEV_MIN_RQ 4

struct inode;
struct device;
struct device_type;
struct request_queue;
struct attribute_group;

struct gendisk {
    char disk_name[32];
    uint32_t major;
    uint32_t first_minor;
    /*
     * Linux mapping:
     * request_queue is per-disk (gendisk), shared by all opens/partitions.
     * Lite keeps a simplified block_device, but the queue ownership follows Linux.
     */
    struct request_queue *queue;
    /* capacity in 512-byte sectors (Linux get_capacity/set_capacity model). */
    uint64_t capacity;
    /* logical block size in bytes (simplified; Linux derives this from queue limits). */
    uint32_t block_size;
    /* Driver-private per-disk data (Linux: gendisk->private_data). */
    void *private_data;
    struct device *dev;
    struct device *parent;
};

struct block_device {
    uint32_t devt;
    /* size in bytes (simplified). Linux derives size from bd_inode/capacity+part. */
    uint64_t size;
    uint32_t block_size;
    uint32_t reads;
    uint32_t writes;
    uint32_t bytes_read;
    uint32_t bytes_written;
    /*
     * Linux mapping:
     * - bdget/bdput provide lifetime references independent of openers.
     * - blkdev_get/blkdev_put manage openers (open file handles).
     */
    uint32_t refcnt;
    uint32_t openers;
    struct gendisk *disk;
    struct inode *inode;
    void *private_data;
};

uint32_t block_device_read(struct block_device *bdev, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t block_device_write(struct block_device *bdev, uint32_t offset, uint32_t size, const uint8_t *buffer);
void block_account_io(struct block_device *bdev, int is_write, uint32_t bytes);
void get_block_stats(uint32_t *reads, uint32_t *writes, uint32_t *bytes_read, uint32_t *bytes_written);
extern const struct device_type disk_type;
int gendisk_init(struct gendisk *disk, const char *name, uint32_t major, uint32_t first_minor);
int add_disk(struct gendisk *disk);
struct device *block_register_disk(struct gendisk *disk, struct device *parent);
struct gendisk *gendisk_from_dev(struct device *dev);
struct inode *blockdev_inode_create(struct block_device *bdev);
void blockdev_inode_destroy(struct block_device *bdev);
struct block_device *bdget(uint32_t devt);
struct block_device *bdget_disk(struct gendisk *disk, int index);
struct block_device *bdgrab(struct block_device *bdev);
void bdput(struct block_device *bdev);
int blkdev_get(struct block_device *bdev);
void blkdev_put(struct block_device *bdev);
int del_gendisk(struct gendisk *disk);
extern const struct attribute_group queue_attr_group;

#endif
