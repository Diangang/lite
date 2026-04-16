#ifndef LINUX_VIRTIO_RING_H
#define LINUX_VIRTIO_RING_H

#include <stdint.h>

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* Linux mapping: include/uapi/linux/virtio_ring.h */
#define VRING_AVAIL_F_NO_INTERRUPT 1
#define VRING_USED_F_NO_NOTIFY     1

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

struct vring {
    unsigned int num;
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
};

static inline void vring_init(struct vring *vr, unsigned int num, void *p, unsigned long align)
{
    vr->num = num;
    vr->desc = (struct vring_desc *)p;
    vr->avail = (struct vring_avail *)((uint8_t *)p + num * sizeof(struct vring_desc));
    vr->used = (struct vring_used *)((((uintptr_t)&vr->avail->ring[num] + sizeof(uint16_t)) + align - 1) & ~(align - 1));
}

static inline unsigned int vring_size(unsigned int num, unsigned long align)
{
    unsigned int size;
    size = num * sizeof(struct vring_desc) + sizeof(uint16_t) * (3 + num);
    size = (size + align - 1) & ~(align - 1);
    size += sizeof(uint16_t) * 3 + num * sizeof(struct vring_used_elem);
    return size;
}

#endif
