#ifndef LINUX_BIO_H
#define LINUX_BIO_H

#include "linux/blk_types.h"

int submit_bio(struct bio *bio);

#endif
