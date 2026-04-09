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

/* generic_make_request: Implement generic make request. */
int generic_make_request(struct bio *bio)
{
    if (!bio || !bio->bi_bdev) {
        bio_complete(bio, -1);
        return -1;
    }
    struct request_queue *q = bio->bi_bdev->queue;
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
        q->request_fn(q);
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
