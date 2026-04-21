#ifndef LINUX_VIRTIO_H
#define LINUX_VIRTIO_H

#include <stdint.h>
#include "linux/kernel.h"
#include "linux/device.h"
#include "linux/virtio_ring.h"

/*
 * Minimal Linux-aligned virtio core types.
 *
 * Linux mapping (2.6+):
 *   - include/linux/virtio.h: struct virtio_device, struct virtio_driver
 *   - drivers/virtio/virtio.c: virtio bus match/probe glue
 *
 * Lite keeps only the subset required by virtio-scsi and virtio-pci transport.
 */

#define VIRTIO_DEV_ANY_ID 0xFFFFFFFFU

/* Virtio device status bits (Linux uapi/linux/virtio_config.h). */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     0x01
#define VIRTIO_CONFIG_S_DRIVER          0x02
#define VIRTIO_CONFIG_S_DRIVER_OK       0x04
#define VIRTIO_CONFIG_S_FEATURES_OK     0x08
#define VIRTIO_CONFIG_S_FAILED          0x80

struct virtio_device;
struct virtio_driver;
struct virtqueue;

struct virtio_device_id {
    uint32_t device;
    uint32_t vendor;
};

struct virtio_config_ops {
    uint64_t (*get_features)(struct virtio_device *vdev);
    void (*set_features)(struct virtio_device *vdev, uint64_t features);
    uint8_t (*get_status)(struct virtio_device *vdev);
    void (*set_status)(struct virtio_device *vdev, uint8_t status);
    void (*reset)(struct virtio_device *vdev);
    int (*get_config)(struct virtio_device *vdev, uint32_t offset, void *buf, uint32_t len);
    int (*find_vqs)(struct virtio_device *vdev, uint16_t nvqs, struct virtqueue **vqs);
    void (*del_vqs)(struct virtio_device *vdev);
};

struct virtio_device {
    struct device dev;
    struct virtio_device_id id;
    const struct virtio_config_ops *config;
    uint32_t index;
    uint64_t features;
    void *priv; /* driver private */
};

static inline struct virtio_device *dev_to_virtio(struct device *dev)
{
    return container_of(dev, struct virtio_device, dev);
}

struct virtio_driver {
    struct device_driver driver;
    const struct virtio_device_id *id_table;
    int (*probe)(struct virtio_device *vdev);
    void (*remove)(struct virtio_device *vdev);
    void (*config_changed)(struct virtio_device *vdev);
};

static inline struct virtio_driver *drv_to_virtio(struct device_driver *drv)
{
    return container_of(drv, struct virtio_driver, driver);
}

extern struct bus_type virtio_bus_type;

int register_virtio_device(struct virtio_device *dev);
void unregister_virtio_device(struct virtio_device *dev);

int register_virtio_driver(struct virtio_driver *drv);
void unregister_virtio_driver(struct virtio_driver *drv);

/* Linux-aligned core helpers (subset). */
void virtio_reset_device(struct virtio_device *vdev);
void virtio_add_status(struct virtio_device *vdev, uint8_t status);
int virtio_finalize_features(struct virtio_device *vdev);
void virtio_device_ready(struct virtio_device *vdev);
void virtio_config_changed(struct virtio_device *vdev);

struct virtqueue_buf {
    uint64_t addr;
    uint32_t len;
    int write; /* device writes to this buffer (VRING_DESC_F_WRITE) */
};

struct virtqueue {
    struct virtio_device *vdev;
    uint16_t index;
    uint16_t num;
    uint16_t last_used_idx;

    uint32_t ring_phys;
    void *ring_virt;
    unsigned int ring_order;
    struct vring vr;

    uint16_t free_head;
    uint16_t num_free;
    uint16_t *desc_next;

    void (*notify)(struct virtqueue *vq);
    /* Transport-private notify address (modern virtio-pci). */
    void *notify_addr;
};

int virtqueue_add_buf(struct virtqueue *vq, const struct virtqueue_buf *bufs, uint16_t nbufs, uint16_t *head);
int virtqueue_kick_prepare(struct virtqueue *vq);
int virtqueue_notify(struct virtqueue *vq);
void virtqueue_kick(struct virtqueue *vq);
int virtqueue_get_buf(struct virtqueue *vq, uint16_t *used_head, uint32_t *len);
int virtqueue_enable_cb(struct virtqueue *vq);
void virtqueue_disable_cb(struct virtqueue *vq);
int virtqueue_wait(struct virtqueue *vq, uint32_t timeout_ticks, uint16_t *used_head);
void virtqueue_free_chain(struct virtqueue *vq, uint16_t head);

#endif
