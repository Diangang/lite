// dir.c - Lite minix dir (Linux mapping: fs/minix/dir.c)

#include "minix.h"

int minix_dir_lookup_entry(struct block_device *bdev, const struct minix_super_block *sb,
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

int minix_dir_add_entry(struct block_device *bdev, const struct minix_super_block *sb,
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

static struct dirent *minix_readdir(struct file *file, uint32_t index)
{
    static struct dirent de_out;
    if (!file || !file->dentry || !file->dentry->inode)
        return NULL;
    struct dentry *d = file->dentry;
    struct inode *node = d->inode;
    if ((node->flags & 0x7) != FS_DIRECTORY)
        return NULL;
    struct minix_inode_info *info = (struct minix_inode_info *)node->private_data;
    if (!info || !info->bdev)
        return NULL;

    if (index == 0) {
        strcpy(de_out.name, ".");
        de_out.ino = node->i_ino;
        return &de_out;
    }
    if (index == 1) {
        strcpy(de_out.name, "..");
        de_out.ino = d->parent && d->parent->inode ? d->parent->inode->i_ino : node->i_ino;
        return &de_out;
    }

    uint32_t want = index - 2;
    uint32_t zsize = minix_zone_size(&info->sb);
    if (zsize == 0)
        return NULL;
    uint32_t dir_size = info->dinode.i_size;
    uint8_t blk[MINIX_BLOCK_SIZE];
    uint32_t pos = 0;
    uint32_t seen = 0;
    while (pos + sizeof(struct minix_dir_entry) <= dir_size) {
        uint16_t zone = minix_zone_for_pos(&info->dinode, &info->sb, pos);
        if (!zone)
            break;
        uint32_t zone_off = pos % zsize;
        uint32_t block = minix_zone_to_block(zone) + (zone_off / MINIX_BLOCK_SIZE);
        uint32_t boff = zone_off % MINIX_BLOCK_SIZE;
        if (minix_bread(info->bdev, block, blk) != MINIX_BLOCK_SIZE)
            return NULL;
        struct minix_dir_entry ent;
        memcpy(&ent, blk + boff, sizeof(ent));
        pos += sizeof(ent);
        if (ent.inode == 0)
            continue;
        if ((ent.name[0] == '.' && ent.name[1] == 0) ||
            (ent.name[0] == '.' && ent.name[1] == '.' && ent.name[2] == 0))
            continue;
        if (seen++ != want)
            continue;

        char name[15];
        memcpy(name, ent.name, 14);
        name[14] = 0;
        if (name[0] == 0)
            return NULL;
        strcpy(de_out.name, name);
        de_out.ino = ent.inode;
        return &de_out;
    }
    return NULL;
}


struct file_operations minix_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = minix_readdir,
    .ioctl = NULL,
};
