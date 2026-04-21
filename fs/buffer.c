#include "linux/buffer_head.h"
#include "linux/blkdev.h"
#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/writeback.h"

#define BH_HASH_BITS 6
#define BH_HASH_SIZE (1u << BH_HASH_BITS)
#define BH_MAX 1024

/* bh_hashfn: Implement bh hashfn. */
static struct buffer_head *bh_hash[BH_HASH_SIZE];
static struct buffer_head *bh_all_head;
static struct buffer_head *bh_all_tail;
static uint32_t bh_total;

static uint32_t bh_hashfn(struct block_device *bdev, uint32_t block, uint32_t size)
{
    uint32_t v = (uint32_t)(uintptr_t)bdev;
    v ^= (block * 2654435761u);
    v ^= (size * 40503u);
    return v & (BH_HASH_SIZE - 1);
}

/* bh_lookup: Implement bh lookup. */
static struct buffer_head *bh_lookup(struct block_device *bdev, uint32_t block, uint32_t size)
{
    uint32_t h = bh_hashfn(bdev, block, size);
    struct buffer_head *bh = bh_hash[h];
    while (bh) {
        if (bh->b_bdev == bdev && bh->b_blocknr == block && bh->b_size == size)
            return bh;
        bh = bh->b_next_hash;
    }
    return NULL;
}

/* bh_all_add_tail: Implement bh all add tail. */
static void bh_all_add_tail(struct buffer_head *bh)
{
    bh->b_prev_all = bh_all_tail;
    bh->b_next_all = NULL;
    if (!bh_all_head)
        bh_all_head = bh;
    if (bh_all_tail)
        bh_all_tail->b_next_all = bh;
    bh_all_tail = bh;
}

/* bh_all_del: Implement bh all del. */
static void bh_all_del(struct buffer_head *bh)
{
    if (bh->b_prev_all)
        bh->b_prev_all->b_next_all = bh->b_next_all;
    else
        bh_all_head = bh->b_next_all;
    if (bh->b_next_all)
        bh->b_next_all->b_prev_all = bh->b_prev_all;
    else
        bh_all_tail = bh->b_prev_all;
    bh->b_prev_all = NULL;
    bh->b_next_all = NULL;
}

/* bh_hash_insert: Implement bh hash insert. */
static void bh_hash_insert(struct buffer_head *bh)
{
    uint32_t h = bh_hashfn(bh->b_bdev, bh->b_blocknr, bh->b_size);
    bh->b_next_hash = bh_hash[h];
    bh_hash[h] = bh;
}

/* bh_hash_remove: Implement bh hash remove. */
static void bh_hash_remove(struct buffer_head *bh)
{
    uint32_t h = bh_hashfn(bh->b_bdev, bh->b_blocknr, bh->b_size);
    struct buffer_head **pp = &bh_hash[h];
    while (*pp) {
        if (*pp == bh) {
            *pp = bh->b_next_hash;
            bh->b_next_hash = NULL;
            return;
        }
        pp = &(*pp)->b_next_hash;
    }
}

/* bh_evict_one: Implement bh evict one. */
static int bh_evict_one(void)
{
    struct buffer_head *bh = bh_all_head;
    while (bh) {
        if (bh->b_count == 0 && (bh->b_state & BH_Dirty) == 0) {
            bh_hash_remove(bh);
            bh_all_del(bh);
            if (bh->b_data)
                kfree(bh->b_data);
            kfree(bh);
            if (bh_total)
                bh_total--;
            return 0;
        }
        bh = bh->b_next_all;
    }
    return -1;
}

/* bh_alloc: Implement bh alloc. */
static struct buffer_head *bh_alloc(struct block_device *bdev, uint32_t block, uint32_t size)
{
    if (bh_total >= BH_MAX) {
        if (bh_evict_one() != 0)
            return NULL;
    }
    struct buffer_head *bh = (struct buffer_head *)kmalloc(sizeof(struct buffer_head));
    if (!bh)
        return NULL;
    memset(bh, 0, sizeof(*bh));
    bh->b_bdev = bdev;
    bh->b_blocknr = block;
    bh->b_size = size;
    bh->b_state = 0;
    bh->b_count = 1;
    bh->b_data = (uint8_t *)kmalloc(size);
    if (!bh->b_data) {
        kfree(bh);
        return NULL;
    }
    bh_hash_insert(bh);
    bh_all_add_tail(bh);
    bh_total++;
    return bh;
}

/* bread: Implement bread. */
struct buffer_head *bread(struct block_device *bdev, uint32_t block, uint32_t size)
{
    if (!bdev || size == 0)
        return NULL;
    struct buffer_head *bh = bh_lookup(bdev, block, size);
    if (bh) {
        bh->b_count++;
    } else {
        bh = bh_alloc(bdev, block, size);
        if (!bh)
            return NULL;
    }
    if ((bh->b_state & BH_Uptodate) == 0) {
        uint32_t offset = block * size;
        uint32_t n = block_device_read(bdev, offset, size, bh->b_data);
        if (n != size) {
            memset(bh->b_data, 0, size);
            bh->b_state &= ~BH_Uptodate;
            brelse(bh);
            return NULL;
        }
        bh->b_state |= BH_Uptodate;
    }
    return bh;
}

/* brelse: Implement brelse. */
void brelse(struct buffer_head *bh)
{
    if (!bh)
        return;
    if (bh->b_count)
        bh->b_count--;
}

/* mark_buffer_dirty: Implement mark buffer dirty. */
void mark_buffer_dirty(struct buffer_head *bh)
{
    if (!bh)
        return;
    if ((bh->b_state & BH_Dirty) == 0)
        writeback_account_dirtied();
    bh->b_state |= BH_Dirty;
}

/* sync_dirty_buffer: Sync dirty buffer. */
int sync_dirty_buffer(struct buffer_head *bh)
{
    if (!bh || !bh->b_bdev || !bh->b_data || bh->b_size == 0)
        return -1;
    if ((bh->b_state & BH_Dirty) == 0)
        return 0;
    uint32_t offset = bh->b_blocknr * bh->b_size;
    uint32_t n = block_device_write(bh->b_bdev, offset, bh->b_size, bh->b_data);
    if (n != bh->b_size)
        return -1;
    bh->b_state &= ~BH_Dirty;
    bh->b_state |= BH_Uptodate;
    writeback_account_cleaned();
    return 0;
}

/* sync_dirty_buffers_all: Sync dirty buffers all. */
int sync_dirty_buffers_all(void)
{
    int flushed = 0;
    struct buffer_head *bh = bh_all_head;
    while (bh) {
        if ((bh->b_state & BH_Dirty) != 0) {
            if (sync_dirty_buffer(bh) == 0)
                flushed++;
        }
        bh = bh->b_next_all;
    }
    return flushed;
}

int sync_mapping_buffers(struct address_space *mapping)
{
    (void)mapping;
    return sync_dirty_buffers_all();
}

/* invalidate_buffer: Invalidate buffer. */
void invalidate_buffer(struct block_device *bdev, uint32_t block, uint32_t size)
{
    if (!bdev || size == 0)
        return;
    struct buffer_head *bh = bh_lookup(bdev, block, size);
    if (!bh)
        return;
    if ((bh->b_state & BH_Dirty) != 0)
        writeback_account_discarded();
    bh->b_state &= ~(BH_Uptodate | BH_Dirty);
}
