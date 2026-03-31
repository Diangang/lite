#ifndef LINUX_BLK_QUEUE_H
#define LINUX_BLK_QUEUE_H

struct bio;
struct request_queue;
struct request;
typedef void (*request_fn_t)(struct request_queue *q);

typedef int (*make_request_fn_t)(struct request_queue *q, struct bio *bio);

struct request_queue {
    make_request_fn_t make_request_fn;
    request_fn_t request_fn;
    struct request *head;
    struct request *tail;
    void *queuedata;
};

int generic_make_request(struct bio *bio);
struct request *blk_fetch_request(struct request_queue *q);
void blk_complete_request(struct request_queue *q, struct request *rq, int error);

#endif
