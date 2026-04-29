// inode.c - Lite minix inode/super (Linux mapping: fs/minix/inode.c)

#include "minix.h"
extern struct inode_operations minix_dir_iops;
extern struct file_operations minix_dir_ops;
extern struct file_operations minix_file_ops;

static void minix_put_super(struct super_block *sb)
{
    if (!sb)
        return;
    sb->fs_private = NULL;
}

static void minix_evict_inode(struct inode *inode)
{
    if (!inode)
        return;
    if (inode->private_data) {
        kfree(inode->private_data);
        inode->private_data = NULL;
    }
    kfree(inode);
}

uint32_t minix_bread(struct block_device *bdev, uint32_t block, void *buf)
{
    struct buffer_head *bh = bread(bdev, block, MINIX_BLOCK_SIZE);
    if (!bh)
        return 0;
    memcpy(buf, bh->b_data, MINIX_BLOCK_SIZE);
    brelse(bh);
    return MINIX_BLOCK_SIZE;
}

uint32_t minix_bwrite(struct block_device *bdev, uint32_t block, const void *buf)
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

int minix_read_super(struct block_device *bdev, struct minix_super_block *sb)
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

int minix_read_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t ino, struct minix_inode_disk *out)
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

uint32_t minix_zone_size(const struct minix_super_block *sb)
{
    return MINIX_BLOCK_SIZE << sb->s_log_zone_size;
}

int minix_write_inode(struct block_device *bdev, const struct minix_super_block *sb,
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

uint32_t minix_zone_to_block(uint16_t zone)
{
    return (uint32_t)zone;
}

uint16_t minix_zone_for_pos(const struct minix_inode_disk *in, const struct minix_super_block *sb, uint32_t pos)
{
    uint32_t zsize = minix_zone_size(sb);
    if (zsize == 0)
        return 0;
    uint32_t z = pos / zsize;
    if (z < 7)
        return in->i_zone[z];
    return 0;
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

struct inode *minix_inode_from_disk(struct block_device *bdev, const struct minix_super_block *sb,
                                    uint32_t ino, struct minix_inode_disk *dinode)
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

static void minix_release_root(struct super_block *sb, struct inode *root_inode)
{
    if (sb && sb->s_root) {
        if (sb->s_root->name)
            kfree((void *)sb->s_root->name);
        kfree(sb->s_root);
        sb->s_root = NULL;
    }
    if (root_inode)
        minix_evict_inode(root_inode);
    minix_put_super(sb);
}

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

    struct minix_inode_disk root_dinode;
    struct minix_super_block msb;
    if (minix_read_super(bdev, &msb) != 0)
        return -1;
    if (minix_read_inode(bdev, &msb, 1, &root_dinode) != 0)
        return -1;

    struct minix_inode_info *root_info = minix_inode_info_new(bdev, &msb, 1, &root_dinode);
    if (!root_info)
        return -1;
    struct inode *root_inode = minix_new_vfs_inode_dir(1, root_info);
    if (!root_inode) {
        minix_put_super(sb);
        kfree(root_info);
        return -1;
    }

    sb->s_root = d_alloc(NULL, "/");
    if (!sb->s_root) {
        minix_release_root(sb, root_inode);
        return -1;
    }
    sb->s_root->inode = root_inode;
    sb->fs_private = bdev;

    return 0;
}

static struct super_block *minix_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    (void)data;
    struct minix_mount_data md;
    md.dev_name = dev_name ? dev_name : "/dev/ram0";
    return vfs_get_sb_single(fs_type, flags, dev_name, &md);
}

/* file_system_type stays in inode.c to match Linux layout. */
static struct file_system_type minix_fs_type = {
    .name = "minix",
    .get_sb = minix_get_sb,
    .fill_super = minix_fill_super,
    .kill_sb = minix_put_super,
    .next = NULL
};

static int init_minix_fs(void)
{
    register_filesystem(&minix_fs_type);
    printf("minix filesystem registered.\n");
    return 0;
}

fs_initcall(init_minix_fs);
