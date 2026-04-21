#include "linux/bio.h"
#include "linux/blkdev.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/errno.h"
#include "linux/list.h"
#include "linux/slab.h"
#include "linux/string.h"
#include <stdint.h>

static inline int blk_irqs_enabled(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags) :: "memory");
    return (flags & 0x200u) != 0;
}

/* bio_endio: Minimal bio completion helper. */
static void bio_endio(struct bio *bio, int error)
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
        bio_endio(bio, ret);
    return ret;
}

int blk_update_nr_requests(struct request_queue *q, unsigned int nr)
{
    if (!q)
        return -EINVAL;
    if (!q->request_fn && !q->make_request_fn)
        return -EINVAL;
    /*
     * Linux mapping: linux2.6/block/blk-core.c:blk_update_nr_requests()
     *
     * Lite simplification: no congestion tracking / request_list accounting;
     * we only update the queue depth which gates submission.
     */
    if (nr < BLKDEV_MIN_RQ)
        nr = BLKDEV_MIN_RQ;
    q->nr_requests = nr;
    return 0;
}

void blk_queue_make_request(struct request_queue *q, make_request_fn *mfn)
{
    if (!q)
        return;
    q->make_request_fn = mfn;
    if (mfn)
        q->request_fn = NULL;
}

void blk_rq_init(struct request_queue *q, struct request *rq)
{
    if (!rq)
        return;
    memset(rq, 0, sizeof(*rq));
    INIT_LIST_HEAD(&rq->queuelist);
    rq->q = q;
}

struct request_queue *blk_init_queue(request_fn_proc *request_fn, void *queuedata)
{
    if (!request_fn)
        return NULL;
    struct request_queue *q = (struct request_queue *)kmalloc(sizeof(*q));
    if (!q)
        return NULL;
    q->make_request_fn = NULL;
    q->request_fn = request_fn;
    INIT_LIST_HEAD(&q->queue_head);
    q->last_merge = NULL;
    q->queuedata = queuedata;
    q->running = 0;
    q->nr_requests = 128;
    q->queued = 0;
    q->in_flight = 0;
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
        bio_endio(bio, -1);
        return -1;
    }
    struct request_queue *q = (bio->bi_bdev->disk) ? bio->bi_bdev->disk->queue : NULL;
    if (!q) {
        bio_endio(bio, -1);
        return -1;
    }
    if (q->request_fn) {
        if (q->nr_requests && (q->queued + q->in_flight) >= q->nr_requests) {
            /*
             * Minimal backpressure: if interrupts are enabled, try to drain
             * once by running request_fn() in submitter context (Lite is sync).
             *
             * Guard:
             * - Do not call into drivers before global `sti` (early boot),
             *   because some transports rely on IRQ-driven completions.
             */
            if (!q->running && blk_irqs_enabled()) {
                q->running = 1;
                q->request_fn(q);
                q->running = 0;
            }
            if ((q->queued + q->in_flight) >= q->nr_requests) {
                bio_endio(bio, -1);
                return -1;
            }
        }
        struct request *rq = (struct request *)kmalloc(sizeof(struct request));
        if (!rq) {
            bio_endio(bio, -1);
            return -1;
        }
        blk_rq_init(q, rq);
        rq->bio = bio;
        list_add_tail(&rq->queuelist, &q->queue_head);
        q->last_merge = rq;
        q->queued++;
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
        bio_endio(bio, -1);
        return -1;
    }
    return q->make_request_fn(q, bio);
}

struct request *blk_peek_request(struct request_queue *q)
{
    if (!q || list_empty(&q->queue_head))
        return NULL;
    return list_first_entry(&q->queue_head, struct request, queuelist);
}

/* blk_fetch_request: Implement block fetch request. */
struct request *blk_fetch_request(struct request_queue *q)
{
    struct request *rq = blk_peek_request(q);
    if (!rq)
        return NULL;
    list_del_init(&rq->queuelist);
    if (q->last_merge == rq)
        q->last_merge = NULL;
    if (q->queued)
        q->queued--;
    q->in_flight++;
    return rq;
}

/* blk_complete_request: Implement block complete request. */
void blk_complete_request(struct request_queue *q, struct request *rq, int error)
{
    if (!rq)
        return;
    if (q && q->in_flight)
        q->in_flight--;
    if (q && q->last_merge == rq)
        q->last_merge = NULL;
    bio_endio(rq->bio, error);
    kfree(rq);
}
