#ifndef LINUX_BLK_REQUEST_H
#define LINUX_BLK_REQUEST_H

struct bio;
struct request_queue;

struct request {
    struct bio *bio;
    struct request *next;
};

typedef void (*request_fn_t)(struct request_queue *q);

#endif
