#ifndef LINUX_BLK_TYPES_H
#define LINUX_BLK_TYPES_H

#include <stdint.h>

typedef uint32_t sector_t;
struct block_device;
struct bio;
typedef void (*bio_end_io_t)(struct bio *bio, int error);

enum {
    REQ_OP_READ = 0,
    REQ_OP_WRITE = 1
};

/*
 * Linux mapping: core bio type lives in include/linux/blk_types.h.
 * Lite keeps a minimal single-buffer subset, but exposes the same ownership.
 */
struct bio {
    struct bio *bi_next;
    struct block_device *bi_bdev;
    sector_t bi_sector;
    uint32_t bi_byte_offset;
    uint32_t bi_size;
    uint8_t *bi_buf;
    uint32_t bi_opf;
    bio_end_io_t bi_end_io;
    void *bi_private;
    int bi_status;
};

#endif
