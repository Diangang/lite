#ifndef FS_MINIX_MINIX_H
#define FS_MINIX_MINIX_H

#include "linux/file.h"
#include "linux/fs.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "linux/pagemap.h"
#include "linux/blkdev.h"
#include "linux/buffer_head.h"

#define MINIX_BLOCK_SIZE 1024
#define MINIX_SUPER_MAGIC 0x137F

struct minix_super_block {
    uint16_t s_ninodes;
    uint16_t s_nzones;
    uint16_t s_imap_blocks;
    uint16_t s_zmap_blocks;
    uint16_t s_firstdatazone;
    uint16_t s_log_zone_size;
    uint32_t s_max_size;
    uint16_t s_magic;
    uint16_t s_state;
} __attribute__((packed));

struct minix_inode_disk {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_mtime;
    uint8_t i_gid;
    uint8_t i_nlinks;
    uint16_t i_zone[9];
} __attribute__((packed));

struct minix_dir_entry {
    uint16_t inode;
    char name[14];
} __attribute__((packed));

struct minix_inode_info {
    struct block_device *bdev;
    uint32_t ino;
    struct minix_super_block sb;
    struct minix_inode_disk dinode;
};

struct minix_mount_data {
    const char *dev_name;
};

/*
 * Linux mapping:
 * - super teardown boundary: put_super()/kill_sb()
 * - inode teardown boundary: evict_inode()
 *
 * Lite VFS does not yet expose a generic umount path, so these helpers are
 * currently used for local error paths and as the canonical ownership boundary
 * for future unmount support.
 */

/* Cross-file ops (Linux mapping: fs/minix/{dir,file,namei}.c) */
extern struct inode_operations minix_dir_iops;
extern struct file_operations minix_dir_ops;
extern struct file_operations minix_file_ops;

/* Shared helpers (Lite subset) */
uint32_t minix_bread(struct block_device *bdev, uint32_t block, void *buf);
uint32_t minix_bwrite(struct block_device *bdev, uint32_t block, const void *buf);
int minix_read_super(struct block_device *bdev, struct minix_super_block *sb);
int minix_read_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t ino, struct minix_inode_disk *out);
int minix_write_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t ino, const struct minix_inode_disk *in);
uint32_t minix_zone_size(const struct minix_super_block *sb);
uint32_t minix_zone_to_block(uint16_t zone);
uint16_t minix_zone_for_pos(const struct minix_inode_disk *in, const struct minix_super_block *sb, uint32_t pos);
struct inode *minix_inode_from_disk(struct block_device *bdev, const struct minix_super_block *sb, uint32_t ino,
                                    struct minix_inode_disk *dinode);
int minix_dir_lookup_entry(struct block_device *bdev, const struct minix_super_block *sb, const struct minix_inode_disk *dir, const char *name, struct minix_dir_entry *de_out, uint32_t *pos_out);
int minix_dir_add_entry(struct block_device *bdev, const struct minix_super_block *sb, struct minix_inode_info *dir_info, uint16_t child_ino, const char *name);
int minix_alloc_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t *ino_out);
int minix_alloc_zone(struct block_device *bdev, const struct minix_super_block *sb, uint16_t *zone_out);

/* NO_DIRECT_LINUX_MATCH: Lite-only helper to seed demo image. */
int minix_seed_example_image(struct block_device *bdev);
int minix_prepare_example_image(struct block_device *bdev);

#endif
