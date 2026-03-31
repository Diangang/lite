#ifndef LINUX_BUFFER_HEAD_H
#define LINUX_BUFFER_HEAD_H

#include <stdint.h>

struct block_device;
struct buffer_head;

enum {
    BH_Uptodate = 1u << 0,
    BH_Dirty = 1u << 1
};

struct buffer_head {
    struct block_device *b_bdev;
    uint32_t b_blocknr;
    uint32_t b_size;
    uint8_t *b_data;
    uint32_t b_state;
    uint32_t b_count;
    struct buffer_head *b_next_hash;
    struct buffer_head *b_prev_all;
    struct buffer_head *b_next_all;
};

struct buffer_head *bread(struct block_device *bdev, uint32_t block, uint32_t size);
void brelse(struct buffer_head *bh);
void mark_buffer_dirty(struct buffer_head *bh);
int sync_dirty_buffer(struct buffer_head *bh);
int sync_dirty_buffers_all(void);
void invalidate_buffer(struct block_device *bdev, uint32_t block, uint32_t size);

#endif
