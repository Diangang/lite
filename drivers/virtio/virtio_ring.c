#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/gfp.h"
#include "linux/slab.h"
#include "linux/time.h"
#include "linux/virtio.h"
#include "linux/virtio_ring.h"

/*
 * Minimal vring helper.
 *
 * Linux mapping:
 *   - drivers/virtio/virtio_ring.c provides virtqueue APIs on top of vring.
 * Lite keeps only the subset needed by virtio-scsi synchronous commands.
 */

#define VQ_DESC_CHAIN_END 0xFFFFU

struct virtqueue *vring_new_virtqueue(unsigned int index, unsigned int num,
                                      unsigned int vring_align,
                                      struct virtio_device *vdev,
                                      void *pages, uint32_t ring_phys,
                                      unsigned int ring_order,
                                      void (*notify)(struct virtqueue *vq),
                                      void *notify_addr)
{
    struct virtqueue *vq;

    if (!vdev || !pages || !notify || num == 0)
        return NULL;

    vq = (struct virtqueue *)kmalloc(sizeof(*vq));
    if (!vq)
        return NULL;
    memset(vq, 0, sizeof(*vq));
    vq->vdev = vdev;
    vq->index = (uint16_t)index;
    vq->num = (uint16_t)num;
    vq->ring_phys = ring_phys;
    vq->ring_virt = pages;
    vq->ring_order = ring_order;
    vq->notify = notify;
    vq->notify_addr = notify_addr;

    vring_init(&vq->vr, num, pages, vring_align);
    vq->desc_next = (uint16_t *)kmalloc(sizeof(uint16_t) * num);
    if (!vq->desc_next) {
        kfree(vq);
        return NULL;
    }
    for (uint16_t d = 0; d < num - 1; d++)
        vq->desc_next[d] = (uint16_t)(d + 1);
    vq->desc_next[num - 1] = VQ_DESC_CHAIN_END;
    vq->free_head = 0;
    vq->num_free = (uint16_t)num;
    return vq;
}

void vring_del_virtqueue(struct virtqueue *vq)
{
    if (!vq)
        return;
    if (vq->desc_next)
        kfree(vq->desc_next);
    if (vq->ring_phys)
        free_pages(vq->ring_phys, vq->ring_order);
    kfree(vq);
}

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
    uint16_t i = head;

    if (!vq)
        return;
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
    uint16_t first = VQ_DESC_CHAIN_END;
    uint16_t prev = VQ_DESC_CHAIN_END;

    if (!vq || !bufs || !nbufs || !head)
        return -1;
    if (vq->num_free < nbufs)
        return -1;

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

    {
        uint16_t slot = (uint16_t)(vq->vr.avail->idx % vq->num);

        vq->vr.avail->ring[slot] = first;
    }
    __asm__ volatile ("" ::: "memory");
    vq->vr.avail->idx++;
    *head = first;
    return 0;
}

int virtqueue_kick_prepare(struct virtqueue *vq)
{
    if (!vq || !vq->notify)
        return 0;
    __asm__ volatile ("" ::: "memory");
    return 1;
}

int virtqueue_notify(struct virtqueue *vq)
{
    if (!vq || !vq->notify)
        return 0;
    vq->notify(vq);
    return 1;
}

void virtqueue_kick(struct virtqueue *vq)
{
    if (virtqueue_kick_prepare(vq))
        (void)virtqueue_notify(vq);
}

int virtqueue_get_buf(struct virtqueue *vq, uint16_t *used_head, uint32_t *len)
{
    uint16_t idx;

    if (!vq || !used_head)
        return 0;
    if (vq->last_used_idx == vq->vr.used->idx)
        return 0;
    idx = (uint16_t)(vq->last_used_idx % vq->num);
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
    uint32_t deadline;
    uint32_t spins = 0;

    if (!vq || !used_head)
        return -1;
    deadline = time_get_jiffies() + timeout_ticks;
    while (!virtqueue_get_buf(vq, used_head, NULL)) {
        if (time_after_eq(time_get_jiffies(), deadline))
            return -1;
        if (++spins > 2000000U)
            return -1;
    }
    return 0;
}
