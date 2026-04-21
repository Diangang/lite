// bitmap.c - Lite minix bitmap (Linux mapping: fs/minix/bitmap.c)

#include "minix.h"

static int minix_bitmap_test(const uint8_t *blk, uint32_t bit)
{
    return (blk[bit >> 3] >> (bit & 7)) & 1u;
}

static void minix_bitmap_set(uint8_t *blk, uint32_t bit)
{
    blk[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

int minix_alloc_inode(struct block_device *bdev, const struct minix_super_block *sb, uint32_t *ino_out)
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

int minix_alloc_zone(struct block_device *bdev, const struct minix_super_block *sb, uint16_t *zone_out)
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

