// namei.c - Lite minix namei (Linux mapping: fs/minix/namei.c)

#include "minix.h"

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
    /* #region debug-point deferred-probe-nvme-minix */
    int watch = (name && !strcmp(name, "nvme_rw.txt"));
    if (watch)
        printf("TRAEDBG {\"ev\":\"minix_create_enter\",\"name\":\"%s\",\"type\":%u}\n", name, type);
    /* #endregion debug-point deferred-probe-nvme-minix */
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
    if (minix_alloc_inode(dir_info->bdev, &dir_info->sb, &ino) != 0) {
        /* #region debug-point deferred-probe-nvme-minix */
        if (watch)
            printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"alloc_inode\"}\n", name);
        /* #endregion debug-point deferred-probe-nvme-minix */
        return NULL;
    }
    if (minix_alloc_zone(dir_info->bdev, &dir_info->sb, &zone) != 0) {
        /* #region debug-point deferred-probe-nvme-minix */
        if (watch)
            printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"alloc_zone\"}\n", name);
        /* #endregion debug-point deferred-probe-nvme-minix */
        return NULL;
    }

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
    if (minix_bwrite(dir_info->bdev, minix_zone_to_block(zone), blk) != MINIX_BLOCK_SIZE) {
        /* #region debug-point deferred-probe-nvme-minix */
        if (watch)
            printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"bwrite\"}\n", name);
        /* #endregion debug-point deferred-probe-nvme-minix */
        return NULL;
    }
    if (minix_write_inode(dir_info->bdev, &dir_info->sb, ino, &dinode) != 0) {
        /* #region debug-point deferred-probe-nvme-minix */
        if (watch)
            printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"write_inode\"}\n", name);
        /* #endregion debug-point deferred-probe-nvme-minix */
        return NULL;
    }
    if (minix_dir_add_entry(dir_info->bdev, &dir_info->sb, dir_info, (uint16_t)ino, name) != 0) {
        /* #region debug-point deferred-probe-nvme-minix */
        if (watch)
            printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"dir_add\"}\n", name);
        /* #endregion debug-point deferred-probe-nvme-minix */
        return NULL;
    }
    if (type == FS_DIRECTORY) {
        dir_info->dinode.i_nlinks++;
        if (minix_write_inode(dir_info->bdev, &dir_info->sb, dir_info->ino, &dir_info->dinode) != 0) {
            /* #region debug-point deferred-probe-nvme-minix */
            if (watch)
                printf("TRAEDBG {\"ev\":\"minix_create_fail\",\"name\":\"%s\",\"step\":\"write_parent\"}\n", name);
            /* #endregion debug-point deferred-probe-nvme-minix */
            return NULL;
        }
    }
    /* #region debug-point deferred-probe-nvme-minix */
    if (watch)
        printf("TRAEDBG {\"ev\":\"minix_create_ok\",\"name\":\"%s\",\"ino\":%u,\"zone\":%u}\n", name, ino, zone);
    /* #endregion debug-point deferred-probe-nvme-minix */
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


struct inode_operations minix_dir_iops = {
    .lookup = minix_dir_finddir,
    .create = minix_create_file,
    .mkdir = minix_mkdir_inode,
    .unlink = NULL,
    .rmdir = NULL
};
