#ifndef LINUX_BIO_H
#define LINUX_BIO_H

#include <stdint.h>

#include "linux/blk_types.h"

struct block_device;
struct bio;

typedef void (*bio_end_io_t)(struct bio *bio, int error);

struct bio {
    struct block_device *bi_bdev;
    sector_t bi_sector;
    uint32_t bi_byte_offset;
    uint32_t bi_size;
    uint8_t *bi_buf;
    uint32_t bi_opf;
    bio_end_io_t bi_end_io;
    int bi_status;
};

int submit_bio(struct bio *bio);

#endif
