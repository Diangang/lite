#include "linux/fs.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/pagemap.h"
#include "linux/blkdev.h"
#include "linux/buffer_head.h"
#include "linux/minixfs.h"

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

static struct inode_operations minix_dir_iops;
static struct file_operations minix_dir_ops;
static struct file_operations minix_file_ops;

/* minix_bread: Implement minix bread. */
static uint32_t minix_bread(struct block_device *bdev, uint32_t block, void *buf)
{
    struct buffer_head *bh = bread(bdev, block, MINIX_BLOCK_SIZE);
    if (!bh)
        return 0;
    memcpy(buf, bh->b_data, MINIX_BLOCK_SIZE);
    brelse(bh);
    return MINIX_BLOCK_SIZE;
}

/* minix_bwrite: Implement minix bwrite. */
static uint32_t minix_bwrite(struct block_device *bdev, uint32_t block, const void *buf)
{
    struct buffer_head *bh = bread(bdev, block, MINIX_BLOCK_SIZE);
    if (!bh)
        return 0;
    memcpy(bh->b_data, buf, MINIX_BLOCK_SIZE);
    mark_buffer_dirty(bh);
    int err = sync_dirty_buffer(bh);
    brelse(bh);
    if (err != 0)
        return 0;
    return MINIX_BLOCK_SIZE;
}

/* minix_read_super: Implement minix read super. */
static int minix_read_super(struct block_device *bdev, struct minix_super_block *sb)
{
    if (!bdev || !sb)
        return -1;
    uint8_t blk[MINIX_BLOCK_SIZE];
    if (minix_bread(bdev, 1, blk) != MINIX_BLOCK_SIZE)
        return -1;
    memcpy(sb, blk, sizeof(*sb));
    if (sb->s_magic != MINIX_SUPER_MAGIC)
        return -1;
    if (sb->s_ninodes == 0 || sb->s_firstdatazone == 0)
        return -1;
    return 0;
}

/* minix_inode_table_block: Implement minix inode table block. */
static uint32_t minix_inode_table_block(const struct minix_super_block *sb)
{
    return 2 + sb->s_imap_blocks + sb->s_zmap_blocks;
}

/* minix_inode_size: Implement minix inode size. */
static uint32_t minix_inode_size(void)
{
    return (uint32_t)sizeof(struct minix_inode_disk);
}

/* minix_read_inode: Implement minix read inode. */
static int minix_read_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t ino, struct minix_inode_disk *out)
{
    if (!bdev || !sb || !out || ino == 0)
        return -1;
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / minix_inode_size();
    if (inodes_per_block == 0)
        return -1;
    if (ino > sb->s_ninodes)
        return -1;
    uint32_t idx = ino - 1;
    uint32_t block = minix_inode_table_block(sb) + (idx / inodes_per_block);
    uint32_t off = (idx % inodes_per_block) * minix_inode_size();
    uint8_t blk[MINIX_BLOCK_SIZE];
    if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
        return -1;
    memcpy(out, blk + off, sizeof(*out));
    return 0;
}

/* minix_zone_size: Implement minix zone size. */
static uint32_t minix_zone_size(const struct minix_super_block *sb)
{
    return MINIX_BLOCK_SIZE << sb->s_log_zone_size;
}

static int minix_bitmap_test(const uint8_t *blk, uint32_t bit)
{
    return (blk[bit >> 3] >> (bit & 7)) & 1u;
}

static void minix_bitmap_set(uint8_t *blk, uint32_t bit)
{
    blk[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

static int minix_write_inode(struct block_device *bdev, const struct minix_super_block *sb,
                             uint32_t ino, const struct minix_inode_disk *in)
{
    if (!bdev || !sb || !in || ino == 0 || ino > sb->s_ninodes)
        return -1;
    uint32_t inode_size = minix_inode_size();
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / inode_size;
    uint32_t idx = ino - 1;
    uint32_t block = minix_inode_table_block(sb) + (idx / inodes_per_block);
    uint32_t off = (idx % inodes_per_block) * inode_size;
    uint8_t blk[MINIX_BLOCK_SIZE];
    if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
        return -1;
    memcpy(blk + off, in, sizeof(*in));
    return minix_bwrite(bdev, block, blk) == MINIX_BLOCK_SIZE ? 0 : -1;
}

static int minix_alloc_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t *ino_out)
{
    if (!bdev || !sb || !ino_out)
        return -1;
    for (uint32_t map = 0; map < sb->s_imap_blocks; map++) {
        uint8_t blk[MINIX_BLOCK_SIZE];
        uint32_t block = 2 + map;
        if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
            return -1;
        for (uint32_t bit = 0; bit < MINIX_BLOCK_SIZE * 8; bit++) {
            uint32_t ino = map * MINIX_BLOCK_SIZE * 8 + bit + 1;
            if (ino == 0 || ino > sb->s_ninodes)
                return -1;
            if (!minix_bitmap_test(blk, bit)) {
                minix_bitmap_set(blk, bit);
                if (minix_bwrite(bdev, block, blk) != MINIX_BLOCK_SIZE)
                    return -1;
                *ino_out = ino;
                return 0;
            }
        }
    }
    return -1;
}

static int minix_alloc_zone(struct block_device *bdev, const struct minix_super_block *sb, uint16_t *zone_out)
{
    if (!bdev || !sb || !zone_out)
        return -1;
    for (uint32_t map = 0; map < sb->s_zmap_blocks; map++) {
        uint8_t blk[MINIX_BLOCK_SIZE];
        uint32_t block = 2 + sb->s_imap_blocks + map;
        if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
            return -1;
        for (uint32_t bit = 0; bit < MINIX_BLOCK_SIZE * 8; bit++) {
            uint32_t zone = sb->s_firstdatazone + map * MINIX_BLOCK_SIZE * 8 + bit;
            if (zone >= sb->s_nzones)
                return -1;
            if (!minix_bitmap_test(blk, bit)) {
                minix_bitmap_set(blk, bit);
                if (minix_bwrite(bdev, block, blk) != MINIX_BLOCK_SIZE)
                    return -1;
                *zone_out = (uint16_t)zone;
                return 0;
            }
        }
    }
    return -1;
}

/* minix_zone_to_block: Implement minix zone to block. */
static uint32_t minix_zone_to_block(uint16_t zone)
{
    return (uint32_t)zone;
}

/* minix_zone_for_pos: Implement minix zone for pos. */
static uint16_t minix_zone_for_pos(const struct minix_inode_disk *in, const struct minix_super_block *sb, uint32_t pos)
{
    uint32_t zsize = minix_zone_size(sb);
    if (zsize == 0)
        return 0;
    uint32_t z = pos / zsize;
    if (z < 7)
        return in->i_zone[z];
    return 0;
}

static int minix_dir_lookup_entry(struct block_device *bdev, const struct minix_super_block *sb,
                                  const struct minix_inode_disk *dir, const char *name,
                                  struct minix_dir_entry *de_out, uint32_t *pos_out)
{
    if (!bdev || !sb || !dir || !name)
        return -1;
    uint32_t dir_size = dir->i_size;
    uint32_t zsize = minix_zone_size(sb);
    if (zsize == 0)
        return -1;
    uint8_t blk[MINIX_BLOCK_SIZE];
    uint32_t pos = 0;
    while (pos + sizeof(struct minix_dir_entry) <= dir_size) {
        uint16_t zone = minix_zone_for_pos(dir, sb, pos);
        if (!zone)
            break;
        uint32_t zone_off = pos % zsize;
        uint32_t block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
        uint32_t boff = zone_off % MINIX_BLOCK_SIZE;
        if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
            return -1;
        struct minix_dir_entry de;
        memcpy(&de, blk + boff, sizeof(de));
        if (de.inode != 0) {
            char entry_name[15];
            memcpy(entry_name, de.name, 14);
            entry_name[14] = 0;
            if (strcmp(entry_name, name) == 0) {
                if (de_out)
                    *de_out = de;
                if (pos_out)
                    *pos_out = pos;
                return 0;
            }
        }
        pos += sizeof(struct minix_dir_entry);
    }
    return -1;
}

static int minix_dir_add_entry(struct block_device *bdev, const struct minix_super_block *sb,
                               struct minix_inode_info *dir_info, uint16_t child_ino, const char *name)
{
    if (!bdev || !sb || !dir_info || !name || !*name)
        return -1;
    if (strlen(name) > 14)
        return -1;
    uint32_t zsize = minix_zone_size(sb);
    if (zsize == 0)
        return -1;
    uint32_t pos = dir_info->dinode.i_size;
    uint16_t zone = minix_zone_for_pos(&dir_info->dinode, sb, pos);
    if (!zone)
        return -1;
    uint32_t zone_off = pos % zsize;
    uint32_t block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
    uint32_t boff = zone_off % MINIX_BLOCK_SIZE;
    if (boff + sizeof(struct minix_dir_entry) > MINIX_BLOCK_SIZE)
        return -1;
    uint8_t blk[MINIX_BLOCK_SIZE];
    if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
        return -1;
    struct minix_dir_entry de;
    memset(&de, 0, sizeof(de));
    de.inode = child_ino;
    memcpy(de.name, name, strlen(name));
    memcpy(blk + boff, &de, sizeof(de));
    if (minix_bwrite(bdev, block, blk) != MINIX_BLOCK_SIZE)
        return -1;
    dir_info->dinode.i_size += sizeof(struct minix_dir_entry);
    return minix_write_inode(bdev, sb, dir_info->ino, &dir_info->dinode);
}

/* minix_file_read: Implement minix file read. */
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

/* minix_file_write: Implement minix file write. */
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

static struct minix_inode_info *minix_inode_info_new(struct block_device *bdev, const struct minix_super_block *sb,
                                                     uint32_t ino, const struct minix_inode_disk *dinode)
{
    if (!bdev || !sb || !dinode)
        return NULL;
    struct minix_inode_info *info = (struct minix_inode_info *)kmalloc(sizeof(*info));
    if (!info)
        return NULL;
    memset(info, 0, sizeof(*info));
    info->bdev = bdev;
    info->ino = ino;
    info->sb = *sb;
    info->dinode = *dinode;
    return info;
}

/* minix_new_vfs_inode_dir: Implement minix new vfs inode dir. */
static struct inode *minix_new_vfs_inode_dir(uint32_t ino, struct minix_inode_info *info)
{
    struct inode *inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_DIRECTORY;
    inode->i_ino = ino ? ino : get_next_ino();
    inode->i_op = &minix_dir_iops;
    inode->f_ops = &minix_dir_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0555;
    inode->i_size = info ? info->dinode.i_size : 0;
    inode->private_data = info;
    return inode;
}

/* minix_new_vfs_inode_file: Implement minix new vfs inode file. */
static struct inode *minix_new_vfs_inode_file(uint32_t ino, struct minix_inode_info *info)
{
    struct inode *inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_FILE;
    inode->i_ino = ino ? ino : get_next_ino();
    inode->i_op = NULL;
    inode->f_ops = &minix_file_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0444;
    inode->i_size = info ? info->dinode.i_size : 0;
    inode->private_data = info;
    return inode;
}

static struct inode *minix_inode_from_disk(struct block_device *bdev, const struct minix_super_block *sb,
                                           uint32_t ino, const struct minix_inode_disk *dinode)
{
    struct minix_inode_info *info = minix_inode_info_new(bdev, sb, ino, dinode);
    if (!info)
        return NULL;
    struct inode *inode = NULL;
    if ((dinode->i_mode & 0170000) == 0040000)
        inode = minix_new_vfs_inode_dir(ino, info);
    else if ((dinode->i_mode & 0170000) == 0100000)
        inode = minix_new_vfs_inode_file(ino, info);
    if (!inode)
        kfree(info);
    return inode;
}

static struct inode *minix_dir_finddir(struct inode *node, const char *name)
{
    if (!node || !name || !*name)
        return NULL;
    if ((node->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    struct minix_inode_info *dir_info = (struct minix_inode_info *)node->private_data;
    if (!dir_info || !dir_info->bdev)
        return NULL;
    struct minix_dir_entry de;
    if (minix_dir_lookup_entry(dir_info->bdev, &dir_info->sb, &dir_info->dinode, name, &de, NULL) != 0)
        return NULL;
    struct minix_inode_disk dinode;
    if (minix_read_inode(dir_info->bdev, &dir_info->sb, de.inode, &dinode) != 0)
        return NULL;
    return minix_inode_from_disk(dir_info->bdev, &dir_info->sb, de.inode, &dinode);
}

static struct inode *minix_create_child(struct inode *dir, const char *name, uint32_t type)
{
    if (!dir || !name || !*name)
        return NULL;
    if ((dir->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strlen(name) > 14)
        return NULL;
    struct minix_inode_info *dir_info = (struct minix_inode_info *)dir->private_data;
    if (!dir_info || !dir_info->bdev)
        return NULL;
    if (minix_dir_lookup_entry(dir_info->bdev, &dir_info->sb, &dir_info->dinode, name, NULL, NULL) == 0)
        return NULL;

    uint32_t ino = 0;
    uint16_t zone = 0;
    if (minix_alloc_inode(dir_info->bdev, &dir_info->sb, &ino) != 0)
        return NULL;
    if (minix_alloc_zone(dir_info->bdev, &dir_info->sb, &zone) != 0)
        return NULL;

    struct minix_inode_disk dinode;
    memset(&dinode, 0, sizeof(dinode));
    dinode.i_mode = (type == FS_DIRECTORY) ? 0040755 : 0100644;
    dinode.i_uid = 0;
    dinode.i_gid = 0;
    dinode.i_nlinks = (type == FS_DIRECTORY) ? 2 : 1;
    dinode.i_size = (type == FS_DIRECTORY) ? 2 * sizeof(struct minix_dir_entry) : 0;
    dinode.i_zone[0] = zone;

    uint8_t blk[MINIX_BLOCK_SIZE];
    memset(blk, 0, sizeof(blk));
    if (type == FS_DIRECTORY) {
        struct minix_dir_entry de;
        memset(&de, 0, sizeof(de));
        de.inode = (uint16_t)ino;
        de.name[0] = '.';
        memcpy(blk + 0 * sizeof(de), &de, sizeof(de));
        memset(&de, 0, sizeof(de));
        de.inode = (uint16_t)dir_info->ino;
        de.name[0] = '.';
        de.name[1] = '.';
        memcpy(blk + 1 * sizeof(de), &de, sizeof(de));
    }
    if (minix_bwrite(dir_info->bdev, minix_zone_to_block(zone), blk) != MINIX_BLOCK_SIZE)
        return NULL;
    if (minix_write_inode(dir_info->bdev, &dir_info->sb, ino, &dinode) != 0)
        return NULL;
    if (minix_dir_add_entry(dir_info->bdev, &dir_info->sb, dir_info, (uint16_t)ino, name) != 0)
        return NULL;
    if (type == FS_DIRECTORY) {
        dir_info->dinode.i_nlinks++;
        if (minix_write_inode(dir_info->bdev, &dir_info->sb, dir_info->ino, &dir_info->dinode) != 0)
            return NULL;
    }
    return minix_inode_from_disk(dir_info->bdev, &dir_info->sb, ino, &dinode);
}

static struct inode *minix_create_file(struct inode *dir, const char *name)
{
    return minix_create_child(dir, name, FS_FILE);
}

static struct inode *minix_mkdir_inode(struct inode *dir, const char *name)
{
    return minix_create_child(dir, name, FS_DIRECTORY);
}

static struct inode_operations minix_dir_iops = {
    .lookup = minix_dir_finddir,
    .create = minix_create_file,
    .mkdir = minix_mkdir_inode,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations minix_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .ioctl = NULL,
};

static struct file_operations minix_file_ops = {
    .read = minix_file_read,
    .write = minix_file_write,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};





int minix_seed_example_image(struct block_device *bdev)
{
    if (!bdev)
        return -1;

    struct minix_super_block sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_ninodes = 16;
    sb.s_nzones = 64;
    sb.s_imap_blocks = 1;
    sb.s_zmap_blocks = 1;
    sb.s_firstdatazone = 5;
    sb.s_log_zone_size = 0;
    sb.s_max_size = 1024 * 1024;
    sb.s_magic = MINIX_SUPER_MAGIC;
    sb.s_state = 1;

    uint8_t blk[MINIX_BLOCK_SIZE];
    memset(blk, 0, sizeof(blk));
    memcpy(blk, &sb, sizeof(sb));
    if (minix_bwrite(bdev, 1, blk) != MINIX_BLOCK_SIZE)
        return -1;

    memset(blk, 0, sizeof(blk));
    blk[0] = 0x03;
    if (minix_bwrite(bdev, 2, blk) != MINIX_BLOCK_SIZE)
        return -1;

    memset(blk, 0, sizeof(blk));
    blk[0] = 0x07;
    if (minix_bwrite(bdev, 3, blk) != MINIX_BLOCK_SIZE)
        return -1;

    struct minix_inode_disk in;
    memset(blk, 0, sizeof(blk));
    memset(&in, 0, sizeof(in));
    in.i_mode = 0040755;
    in.i_uid = 0;
    in.i_gid = 0;
    in.i_nlinks = 2;
    in.i_size = 3 * sizeof(struct minix_dir_entry);
    in.i_zone[0] = 5;
    memcpy(blk + 0 * sizeof(in), &in, sizeof(in));

    const char *msg = "Hello from MinixFS!\n";
    uint32_t msg_len = (uint32_t)strlen(msg);
    memset(&in, 0, sizeof(in));
    in.i_mode = 0100444;
    in.i_uid = 0;
    in.i_gid = 0;
    in.i_nlinks = 1;
    in.i_size = msg_len;
    in.i_zone[0] = 6;
    memcpy(blk + 1 * sizeof(in), &in, sizeof(in));
    if (minix_bwrite(bdev, 4, blk) != MINIX_BLOCK_SIZE)
        return -1;

    memset(blk, 0, sizeof(blk));
    struct minix_dir_entry de;
    memset(&de, 0, sizeof(de));
    de.inode = 1;
    de.name[0] = '.';
    memcpy(blk + 0 * sizeof(de), &de, sizeof(de));
    memset(&de, 0, sizeof(de));
    de.inode = 1;
    de.name[0] = '.';
    de.name[1] = '.';
    memcpy(blk + 1 * sizeof(de), &de, sizeof(de));
    memset(&de, 0, sizeof(de));
    de.inode = 2;
    memcpy(de.name, "hello.txt", 9);
    memcpy(blk + 2 * sizeof(de), &de, sizeof(de));
    if (minix_bwrite(bdev, 5, blk) != MINIX_BLOCK_SIZE)
        return -1;

    memset(blk, 0, sizeof(blk));
    memcpy(blk, msg, msg_len);
    if (minix_bwrite(bdev, 6, blk) != MINIX_BLOCK_SIZE)
        return -1;

    return 0;
}

int minix_prepare_example_image(struct block_device *bdev)
{
    struct minix_super_block sb;
    if (!bdev)
        return -1;
    if (minix_read_super(bdev, &sb) == 0)
        return 0;
    return minix_seed_example_image(bdev);
}

/* minix_fill_super: Implement minix fill super. */
static int minix_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)silent;
    if (!sb || !data)
        return -1;
    struct minix_mount_data *md = (struct minix_mount_data *)data;
    if (!md->dev_name)
        return -1;

    /* #region debug-point scsi-nvme-smoke.minix-fill-super */
    printf("debug(scsi-nvme-smoke): minix_fill_super dev=%s\n", md->dev_name);
    /* #endregion */

    struct inode *dev_inode = vfs_resolve(md->dev_name);
    /* #region debug-point scsi-nvme-smoke.minix-dev-inode */
    printf("debug(scsi-nvme-smoke): vfs_resolve(%s) => %s\n",
           md->dev_name, dev_inode ? "OK" : "NULL");
    /* #endregion */
    if (!dev_inode || (dev_inode->flags & 0x7) != FS_BLOCKDEVICE)
        return -1;
    struct block_device *bdev = (struct block_device *)dev_inode->private_data;
    if (!bdev)
        return -1;

    struct minix_inode_disk root_dinode;
    struct minix_super_block msb;
    if (minix_read_super(bdev, &msb) != 0) {
        /* #region debug-point scsi-nvme-smoke.minix-read-super-fail */
        printf("debug(scsi-nvme-smoke): minix_read_super failed dev=%s\n", md->dev_name);
        /* #endregion */
        return -1;
    }
    if (minix_read_inode(bdev, &msb, 1, &root_dinode) != 0)
        return -1;

    struct minix_inode_info *root_info = minix_inode_info_new(bdev, &msb, 1, &root_dinode);
    if (!root_info)
        return -1;
    struct inode *root_inode = minix_new_vfs_inode_dir(1, root_info);
    if (!root_inode) {
        kfree(root_info);
        return -1;
    }

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = root_inode;
    sb->fs_private = bdev;

    uint32_t dir_size = root_dinode.i_size;
    uint32_t zsize = minix_zone_size(&msb);
    if (zsize == 0)
        return -1;

    uint8_t blk[MINIX_BLOCK_SIZE];
    uint32_t pos = 0;
    while (pos + sizeof(struct minix_dir_entry) <= dir_size) {
        uint16_t zone = minix_zone_for_pos(&root_dinode, &msb, pos);
        if (!zone)
            break;
        uint32_t zone_off = pos % zsize;
        uint32_t block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
        uint32_t boff = zone_off % MINIX_BLOCK_SIZE;
        if (minix_bread(bdev, block, blk) != MINIX_BLOCK_SIZE)
            break;
        struct minix_dir_entry de;
        memcpy(&de, blk + boff, sizeof(de));
        pos += sizeof(de);
        if (de.inode == 0)
            continue;
        if ((de.name[0] == '.' && de.name[1] == 0) ||
            (de.name[0] == '.' && de.name[1] == '.' && de.name[2] == 0))
            continue;
        char name[15];
        memcpy(name, de.name, 14);
        name[14] = 0;
        uint32_t nlen = (uint32_t)strlen(name);
        if (nlen == 0)
            continue;

        struct minix_inode_disk child_dinode;
        if (minix_read_inode(bdev, &msb, de.inode, &child_dinode) != 0)
            continue;
        {
            struct inode *child = minix_inode_from_disk(bdev, &msb, de.inode, &child_dinode);
            if (!child)
                continue;
            struct dentry *d = d_alloc(sb->s_root, name);
            if (!d)
                continue;
            d->inode = child;
        }
    }

    return 0;
}

/* minix_get_sb: Implement minix get sb. */
static struct super_block *minix_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)data;
    struct minix_mount_data md;
    md.dev_name = dev_name ? dev_name : "/dev/ram0";
    return vfs_get_sb_single(fs_type, flags, dev_name, &md);
}

static struct file_system_type minix_fs_type = {
    .name = "minix",
    .get_sb = minix_get_sb,
    .fill_super = minix_fill_super,
    .kill_sb = NULL,
    .next = NULL
};

/* init_minix_fs: Initialize minix fs. */
static int init_minix_fs(void)
{
    register_filesystem(&minix_fs_type);
    printf("minix filesystem registered.\n");
    return 0;
}

fs_initcall(init_minix_fs);
