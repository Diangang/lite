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
    int running; /* prevent recursive request_fn entry */
    unsigned int nr_requests; /* Linux mapping: queue depth limit */
    unsigned int queued;      /* pending requests linked on q->head */
    unsigned int in_flight;   /* requests fetched by driver, not yet completed */
};

/* Linux-aligned helpers (single-queue, non-blk-mq). */
struct request_queue *blk_init_queue(request_fn_t request_fn, void *queuedata);
void blk_cleanup_queue(struct request_queue *q);

/* Linux mapping: block/blk-core.c:blk_update_nr_requests() */
int blk_update_nr_requests(struct request_queue *q, unsigned int nr);

int generic_make_request(struct bio *bio);
struct request *blk_fetch_request(struct request_queue *q);
void blk_complete_request(struct request_queue *q, struct request *rq, int error);

#endif
