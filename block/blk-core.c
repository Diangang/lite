#include "linux/bio.h"
#include "linux/blkdev.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/slab.h"

/* bio_complete: Implement bio complete. */
static void bio_complete(struct bio *bio, int error)
{
    if (!bio)
        return;
    bio->bi_status = error;
    if (bio->bi_end_io)
        bio->bi_end_io(bio, error);
}

/* submit_bio: Push a bio into the block layer queue. */
int submit_bio(struct bio *bio)
{
    if (!bio || !bio->bi_bdev || !bio->bi_buf || bio->bi_size == 0)
        return -1;
    int ret = generic_make_request(bio);
    if (ret != 0 && bio->bi_status == 0)
        bio_complete(bio, ret);
    return ret;
}

struct request_queue *blk_init_queue(request_fn_t request_fn, void *queuedata)
{
    if (!request_fn)
        return NULL;
    struct request_queue *q = (struct request_queue *)kmalloc(sizeof(*q));
    if (!q)
        return NULL;
    q->make_request_fn = NULL;
    q->request_fn = request_fn;
    q->head = NULL;
    q->tail = NULL;
    q->queuedata = queuedata;
    q->running = 0;
    return q;
}

void blk_cleanup_queue(struct request_queue *q)
{
    if (!q)
        return;
    /* Drop any pending requests as failed. */
    while (1) {
        struct request *rq = blk_fetch_request(q);
        if (!rq)
            break;
        blk_complete_request(q, rq, -1);
    }
    kfree(q);
}

/* generic_make_request: Implement generic make request. */
int generic_make_request(struct bio *bio)
{
    if (!bio || !bio->bi_bdev) {
        bio_complete(bio, -1);
        return -1;
    }
    struct request_queue *q = (bio->bi_bdev->disk) ? bio->bi_bdev->disk->queue : NULL;
    if (!q) {
        bio_complete(bio, -1);
        return -1;
    }
    if (q->request_fn) {
        struct request *rq = (struct request *)kmalloc(sizeof(struct request));
        if (!rq) {
            bio_complete(bio, -1);
            return -1;
        }
        rq->bio = bio;
        rq->next = NULL;
        if (!q->head) {
            q->head = rq;
            q->tail = rq;
        } else {
            q->tail->next = rq;
            q->tail = rq;
        }
        /*
         * Run the queue synchronously in the submitter context (simplest single-queue).
         * Prevent recursive entry if request_fn triggers nested submit_bio().
         */
        if (!q->running) {
            q->running = 1;
            q->request_fn(q);
            q->running = 0;
        }
        return 0;
    }
    if (!q->make_request_fn) {
        bio_complete(bio, -1);
        return -1;
    }
    return q->make_request_fn(q, bio);
}

/* blk_fetch_request: Implement block fetch request. */
struct request *blk_fetch_request(struct request_queue *q)
{
    if (!q || !q->head)
        return NULL;
    struct request *rq = q->head;
    q->head = rq->next;
    if (!q->head)
        q->tail = NULL;
    rq->next = NULL;
    return rq;
}

/* blk_complete_request: Implement block complete request. */
void blk_complete_request(struct request_queue *q, struct request *rq, int error)
{
    (void)q;
    if (!rq)
        return;
    bio_complete(rq->bio, error);
    kfree(rq);
}
