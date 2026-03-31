#include "linux/fs.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/pagemap.h"
#include "linux/blkdev.h"

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
    struct minix_super_block sb;
    struct minix_inode_disk dinode;
};

struct minix_mount_data {
    const char *dev_name;
};

static uint32_t minix_bread(struct block_device *bdev, uint32_t block, void *buf)
{
    return block_device_read(bdev, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf);
}

static uint32_t minix_bwrite(struct block_device *bdev, uint32_t block, const void *buf)
{
    return block_device_write(bdev, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (const uint8_t *)buf);
}

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

static uint32_t minix_inode_table_block(const struct minix_super_block *sb)
{
    return 2 + sb->s_imap_blocks + sb->s_zmap_blocks;
}

static uint32_t minix_inode_size(void)
{
    return (uint32_t)sizeof(struct minix_inode_disk);
}

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

static uint32_t minix_zone_size(const struct minix_super_block *sb)
{
    return MINIX_BLOCK_SIZE << sb->s_log_zone_size;
}

static uint32_t minix_zone_to_block(uint16_t zone)
{
    return (uint32_t)zone;
}

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

static struct file_operations minix_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = generic_readdir,
    .finddir = NULL,
    .ioctl = NULL,
    .unlink = NULL,
    .rmdir = NULL
};

static struct file_operations minix_file_ops = {
    .read = minix_file_read,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .ioctl = NULL
};

static struct inode *minix_new_vfs_inode_dir(uint32_t ino)
{
    struct inode *inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_DIRECTORY;
    inode->i_ino = ino ? ino : get_next_ino();
    inode->f_ops = &minix_dir_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0555;
    return inode;
}

static struct inode *minix_new_vfs_inode_file(uint32_t ino, struct minix_inode_info *info)
{
    struct inode *inode = (struct inode *)kmalloc(sizeof(struct inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->flags = FS_FILE;
    inode->i_ino = ino ? ino : get_next_ino();
    inode->f_ops = &minix_file_ops;
    inode->uid = 0;
    inode->gid = 0;
    inode->i_mode = 0444;
    inode->i_size = info ? info->dinode.i_size : 0;
    inode->private_data = info;
    return inode;
}

static void minix_write_example_image(struct block_device *bdev)
{
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
    minix_bwrite(bdev, 1, blk);

    memset(blk, 0, sizeof(blk));
    blk[0] = 0x03;
    minix_bwrite(bdev, 2, blk);

    memset(blk, 0, sizeof(blk));
    blk[0] = 0x07;
    minix_bwrite(bdev, 3, blk);

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
    minix_bwrite(bdev, 4, blk);

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
    const char *fn = "hello.txt";
    memcpy(de.name, fn, strlen(fn));
    memcpy(blk + 2 * sizeof(de), &de, sizeof(de));
    minix_bwrite(bdev, 5, blk);

    memset(blk, 0, sizeof(blk));
    memcpy(blk, msg, msg_len);
    minix_bwrite(bdev, 6, blk);
}

static int minix_fill_super(struct super_block *sb, void *data, int silent)
{
    (void)silent;
    if (!sb || !data)
        return -1;
    struct minix_mount_data *md = (struct minix_mount_data *)data;
    if (!md->dev_name)
        return -1;
    struct inode *dev_inode = vfs_resolve(md->dev_name);
    if (!dev_inode || (dev_inode->flags & 0x7) != FS_BLOCKDEVICE)
        return -1;
    struct block_device *bdev = (struct block_device *)dev_inode->private_data;
    if (!bdev)
        return -1;

    struct minix_super_block msb;
    if (minix_read_super(bdev, &msb) != 0) {
        minix_write_example_image(bdev);
        if (minix_read_super(bdev, &msb) != 0)
            return -1;
    }

    struct inode *root_inode = minix_new_vfs_inode_dir(1);
    if (!root_inode)
        return -1;

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root)
        return -1;
    sb->s_root->inode = root_inode;
    sb->fs_private = bdev;

    struct minix_inode_disk root_dinode;
    if (minix_read_inode(bdev, &msb, 1, &root_dinode) != 0)
        return 0;

    uint32_t dir_size = root_dinode.i_size;
    uint32_t zsize = minix_zone_size(&msb);
    if (zsize == 0)
        return 0;

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
        if ((child_dinode.i_mode & 0170000) == 0040000) {
            struct inode *child = minix_new_vfs_inode_dir(de.inode);
            if (!child)
                continue;
            child->i_mode = 0555;
            child->i_size = child_dinode.i_size;
            struct dentry *d = d_alloc(sb->s_root, name);
            if (!d)
                continue;
            d->inode = child;
        } else if ((child_dinode.i_mode & 0170000) == 0100000) {
            struct minix_inode_info *info = (struct minix_inode_info *)kmalloc(sizeof(struct minix_inode_info));
            if (!info)
                continue;
            memset(info, 0, sizeof(*info));
            info->bdev = bdev;
            info->sb = msb;
            info->dinode = child_dinode;
            struct inode *child = minix_new_vfs_inode_file(de.inode, info);
            if (!child) {
                kfree(info);
                continue;
            }
            struct dentry *d = d_alloc(sb->s_root, name);
            if (!d)
                continue;
            d->inode = child;
        }
    }

    return 0;
}

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

static int init_minix_fs(void)
{
    register_filesystem(&minix_fs_type);
    printf("minix filesystem registered.\n");
    return 0;
}

module_init(init_minix_fs);

