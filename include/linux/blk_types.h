#ifndef LINUX_BLK_TYPES_H
#define LINUX_BLK_TYPES_H

#include <stdint.h>

typedef uint32_t sector_t;

enum {
    REQ_OP_READ = 0,
    REQ_OP_WRITE = 1
};

#endif
