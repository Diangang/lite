#include "linux/libc.h"
#include "linux/slab.h"
#include "linux/timer.h"
#include "linux/virtio.h"

/*
 * Minimal virtqueue helper (vring-based).
 *
 * Linux mapping:
 *   - drivers/virtio/virtio_ring.c provides virtqueue APIs on top of vring.
 * Lite keeps only the subset needed by virtio-scsi synchronous commands.
 */

#define VQ_DESC_CHAIN_END 0xFFFFU

static int vq_alloc_desc(struct virtqueue *vq, uint16_t *id)
{
    if (!vq || !id || vq->num_free == 0)
        return -1;
    *id = vq->free_head;
    vq->free_head = vq->desc_next[*id];
    vq->num_free--;
    return 0;
}

static void vq_free_desc(struct virtqueue *vq, uint16_t id)
{
    vq->desc_next[id] = vq->free_head;
    vq->free_head = id;
    vq->num_free++;
}

void virtqueue_free_chain(struct virtqueue *vq, uint16_t head)
{
    if (!vq)
        return;
    uint16_t i = head;
    for (;;) {
        uint16_t next = VQ_DESC_CHAIN_END;
        if (vq->vr.desc[i].flags & VRING_DESC_F_NEXT)
            next = vq->vr.desc[i].next;
        vq_free_desc(vq, i);
        if (next == VQ_DESC_CHAIN_END)
            break;
        i = next;
    }
}

int virtqueue_add_buf(struct virtqueue *vq, const struct virtqueue_buf *bufs, uint16_t nbufs, uint16_t *head)
{
    if (!vq || !bufs || !nbufs || !head)
        return -1;
    if (vq->num_free < nbufs)
        return -1;

    uint16_t first = VQ_DESC_CHAIN_END;
    uint16_t prev = VQ_DESC_CHAIN_END;

    for (uint16_t i = 0; i < nbufs; i++) {
        uint16_t id;
        if (vq_alloc_desc(vq, &id) != 0) {
            if (first != VQ_DESC_CHAIN_END)
                virtqueue_free_chain(vq, first);
            return -1;
        }
        if (first == VQ_DESC_CHAIN_END)
            first = id;

        vq->vr.desc[id].addr = bufs[i].addr;
        vq->vr.desc[id].len = bufs[i].len;
        vq->vr.desc[id].flags = (uint16_t)(bufs[i].write ? VRING_DESC_F_WRITE : 0);
        vq->vr.desc[id].next = 0;

        if (prev != VQ_DESC_CHAIN_END) {
            vq->vr.desc[prev].flags |= VRING_DESC_F_NEXT;
            vq->vr.desc[prev].next = id;
        }
        prev = id;
    }

    uint16_t slot = (uint16_t)(vq->vr.avail->idx % vq->num);
    vq->vr.avail->ring[slot] = first;
    __asm__ volatile ("" ::: "memory");
    vq->vr.avail->idx++;
    *head = first;
    return 0;
}

void virtqueue_kick(struct virtqueue *vq)
{
    if (!vq || !vq->notify)
        return;
    /*
     * Linux mapping: virtqueue_kick() notifies the device that new buffers are available.
     * Lite always kicks for simplicity; VIRTQ_USED_F_NO_NOTIFY is ignored.
     */
    __asm__ volatile ("" ::: "memory");
    vq->notify(vq);
}

int virtqueue_get_buf(struct virtqueue *vq, uint16_t *used_head, uint32_t *len)
{
    if (!vq || !used_head)
        return 0;
    if (vq->last_used_idx == vq->vr.used->idx)
        return 0;
    uint16_t idx = (uint16_t)(vq->last_used_idx % vq->num);
    *used_head = (uint16_t)vq->vr.used->ring[idx].id;
    if (len)
        *len = vq->vr.used->ring[idx].len;
    vq->last_used_idx++;
    return 1;
}

int virtqueue_enable_cb(struct virtqueue *vq)
{
    if (!vq)
        return 0;
    /*
     * Linux mapping: virtqueue_enable_cb() clears NO_INTERRUPT and returns whether
     * there are no used buffers pending.
     */
    vq->vr.avail->flags = (uint16_t)(vq->vr.avail->flags & (uint16_t)~VRING_AVAIL_F_NO_INTERRUPT);
    __asm__ volatile ("" ::: "memory");
    return vq->last_used_idx == vq->vr.used->idx;
}

void virtqueue_disable_cb(struct virtqueue *vq)
{
    if (!vq)
        return;
    vq->vr.avail->flags = (uint16_t)(vq->vr.avail->flags | VRING_AVAIL_F_NO_INTERRUPT);
}

int virtqueue_wait(struct virtqueue *vq, uint32_t timeout_ticks, uint16_t *used_head)
{
    if (!vq || !used_head)
        return -1;
    uint32_t start = timer_get_ticks();
    uint32_t spins = 0;
    while (!virtqueue_get_buf(vq, used_head, NULL)) {
        /*
         * Avoid unbounded busy-wait: early boot may not have a ticking clock.
         * Linux uses proper wait/irq callbacks; Lite keeps a bounded poll.
         */
        uint32_t now = timer_get_ticks();
        if (now - start > timeout_ticks)
            return -1;
        if (++spins > 2000000U)
            return -1;
    }
    return 0;
}
