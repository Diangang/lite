#ifndef LINUX_MINIXFS_H
#define LINUX_MINIXFS_H

struct block_device;

/*
 * Seed a minimal Minix filesystem image onto a blank block device.
 * This is used only for the in-memory ramdisk fallback path.
 */
int minix_seed_example_image(struct block_device *bdev);

#endif
