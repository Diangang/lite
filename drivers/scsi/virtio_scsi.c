#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"
#include "linux/gfp.h"
#include "linux/slab.h"
#include "linux/time.h"
#include "linux/virtio.h"
#include "linux/virtio_ids.h"
#include "linux/virtio_ring.h"
#include "linux/virtio_scsi.h"
#include "linux/blkdev.h"
#include "asm/pgtable.h"
#include "asm/pgtable.h"
#include "base.h"
#include "scsi/scsi.h"
#include "scsi/scsi_host.h"

#define VIRTIO_SCSI_QUEUE_CTRL  0
#define VIRTIO_SCSI_QUEUE_EVENT 1
#define VIRTIO_SCSI_QUEUE_REQ   2

struct virtio_scsi_target_state {
    struct virtqueue *req_vq;
};

struct virtio_scsi {
    struct virtio_device *vdev;
    struct virtio_scsi_config cfg;
    struct virtqueue *ctrl_vq;
    struct virtqueue *event_vq;
    struct virtqueue *req_vq;
    uint32_t event_phys;
    struct virtio_scsi_event *event;
    struct Scsi_Host *host;
};

static void virtscsi_remove(struct virtio_device *vdev);

static int virtio_scsi_read_config(struct virtio_scsi *vscsi)
{
    if (!vscsi || !vscsi->vdev || !vscsi->vdev->config || !vscsi->vdev->config->get_config)
        return -1;
    memset(&vscsi->cfg, 0, sizeof(vscsi->cfg));
    return vscsi->vdev->config->get_config(vscsi->vdev, 0, &vscsi->cfg, sizeof(vscsi->cfg));
}

static void virtio_scsi_init_lun(struct virtio_scsi_cmd_req *req, struct scsi_device *sdev)
{
    memset(req, 0, sizeof(*req));
    req->lun[0] = 1;
    req->lun[1] = (uint8_t)sdev->id;
    req->lun[2] = (uint8_t)(((uint16_t)sdev->lun >> 8) | 0x40);
    req->lun[3] = (uint8_t)sdev->lun;
    req->task_attr = VIRTIO_SCSI_S_SIMPLE;
}

static int virtio_scsi_submit(struct virtio_scsi *vscsi, struct scsi_cmnd *sc)
{
    if (!vscsi || !sc || !sc->device || sc->cmd_len > VIRTIO_SCSI_CDB_DEFAULT_SIZE)
        return -1;
    struct scsi_device *sdev = sc->device;
    struct scsi_target *starget = scsi_target(sdev);
    struct virtio_scsi_target_state *tgt = starget ? (struct virtio_scsi_target_state *)starget->hostdata : NULL;
    struct virtqueue *vq = tgt && tgt->req_vq ? tgt->req_vq : vscsi->req_vq;
    /*
     * DMA safety: avoid placing request/response on the kernel stack.
     * Linux allocates request structures from heap/slab and maps them for DMA.
     * Lite uses virt_to_phys() + direct DMA, so use kmalloc-backed buffers.
     */
    struct virtio_scsi_cmd_req *req = (struct virtio_scsi_cmd_req *)kmalloc(sizeof(*req));
    struct virtio_scsi_cmd_resp *resp = (struct virtio_scsi_cmd_resp *)kmalloc(sizeof(*resp));
    if (!req || !resp) {
        kfree(req);
        kfree(resp);
        return -1;
    }
    memset(resp, 0, sizeof(*resp));
    virtio_scsi_init_lun(req, sdev);
    memcpy(req->cdb, sc->cmnd, sc->cmd_len);

    struct virtqueue_buf bufs[4];
    uint16_t nbufs = 0;
    bufs[nbufs++] = (struct virtqueue_buf){ .addr = virt_to_phys(req), .len = sizeof(*req), .write = 0 };
    if (sc->request_buffer && sc->request_bufflen && sc->sc_data_direction == DMA_TO_DEVICE)
        bufs[nbufs++] = (struct virtqueue_buf){ .addr = virt_to_phys(sc->request_buffer),
                                                .len = sc->request_bufflen, .write = 0 };
    bufs[nbufs++] = (struct virtqueue_buf){ .addr = virt_to_phys(resp), .len = sizeof(*resp), .write = 1 };
    if (sc->request_buffer && sc->request_bufflen && sc->sc_data_direction == DMA_FROM_DEVICE)
        bufs[nbufs++] = (struct virtqueue_buf){ .addr = virt_to_phys(sc->request_buffer),
                                                .len = sc->request_bufflen, .write = 1 };

    uint16_t head = 0;
    if (virtqueue_add_buf(vq, bufs, nbufs, &head) != 0) {
        kfree(req);
        kfree(resp);
        return -1;
    }
    virtqueue_kick(vq);
    uint16_t used_head = 0;
    if (virtqueue_wait(vq, HZ, &used_head) != 0) {
        virtqueue_free_chain(vq, head);
        sc->result = SCSI_STATUS_CHECK_CONDITION;
        if (sc->scsi_done)
            sc->scsi_done(sc);
        kfree(req);
        kfree(resp);
        return 0;
    }
    virtqueue_free_chain(vq, used_head);

    sc->sense_len = 0;
    if (resp->sense_len && resp->sense_len <= SCSI_SENSE_BUFFERSIZE) {
        memcpy(sc->sense_buffer, resp->sense, resp->sense_len);
        sc->sense_len = resp->sense_len;
    } else if (resp->sense_len) {
        memcpy(sc->sense_buffer, resp->sense, SCSI_SENSE_BUFFERSIZE);
        sc->sense_len = SCSI_SENSE_BUFFERSIZE;
    }
    sc->result = resp->status;
    if (sc->scsi_done)
        sc->scsi_done(sc);
    kfree(req);
    kfree(resp);
    return 0;
}

static int virtscsi_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *sc)
{
    struct virtio_scsi *vscsi = (struct virtio_scsi *)shost->hostdata;
    return virtio_scsi_submit(vscsi, sc);
}

static int virtscsi_target_alloc(struct scsi_target *starget)
{
    if (!starget || !starget->host)
        return -1;
    struct virtio_scsi *vscsi = (struct virtio_scsi *)starget->host->hostdata;
    if (!vscsi)
        return -1;

    struct virtio_scsi_target_state *tgt =
        (struct virtio_scsi_target_state *)kmalloc(sizeof(*tgt));
    if (!tgt)
        return -1;
    memset(tgt, 0, sizeof(*tgt));
    tgt->req_vq = vscsi->req_vq;
    starget->hostdata = tgt;
    return 0;
}

static void virtscsi_target_destroy(struct scsi_target *starget)
{
    if (!starget)
        return;
    kfree(starget->hostdata);
    starget->hostdata = NULL;
}

static struct scsi_host_template virtscsi_sht = {
    .name = "virtio_scsi",
    .queuecommand = virtscsi_queuecommand,
    .target_alloc = virtscsi_target_alloc,
    .target_destroy = virtscsi_target_destroy,
};

static int virtscsi_scan(struct virtio_scsi *vscsi)
{
    if (!vscsi)
        return -1;
    vscsi->host = scsi_host_alloc(&virtscsi_sht, vscsi);
    if (!vscsi->host)
        return -1;
    /*
     * Linux mapping: host->max_id/max_lun describe addressing limits, but Linux
     * does not naively scan the full cartesian product at boot.
     *
     * Lite's scsi_scan_host() is a simple enumerator over [0..max_id) and can
     * explode if max_target/max_lun are large (virtio-scsi commonly reports
     * wide ranges). Keep a conservative scan window so boot completes within
     * smoke timeouts; real device discovery can be expanded later.
     */
    vscsi->host->max_channel = 0;
    vscsi->host->max_id = 1;   /* target 0 only */
    vscsi->host->max_lun = 1;  /* LUN 0 only */
    int ret = scsi_add_host(vscsi->host, &vscsi->vdev->dev);
    if (ret != 0)
        return -1;
    return scsi_scan_host(vscsi->host);
}

static int virtscsi_probe(struct virtio_device *vdev)
{
    if (!vdev || !vdev->config)
        return -1;
    struct virtio_scsi *vscsi = (struct virtio_scsi *)kmalloc(sizeof(*vscsi));
    if (!vscsi)
        return -1;
    memset(vscsi, 0, sizeof(*vscsi));
    vscsi->vdev = vdev;
    vdev->priv = vscsi;

    if (virtio_scsi_read_config(vscsi) != 0)
        goto err_remove;
    if (!vdev->config->find_vqs || !vdev->config->del_vqs)
        goto err_remove;
    struct virtqueue *vqs[3] = { 0 };
    if (vdev->config->find_vqs(vdev, 3, vqs) != 0)
        goto err_remove;
    vscsi->ctrl_vq = vqs[VIRTIO_SCSI_QUEUE_CTRL];
    vscsi->event_vq = vqs[VIRTIO_SCSI_QUEUE_EVENT];
    vscsi->req_vq = vqs[VIRTIO_SCSI_QUEUE_REQ];

    void *evt_phys = alloc_page(GFP_KERNEL);
    if (!evt_phys)
        goto err_remove;
    vscsi->event_phys = (uint32_t)evt_phys;
    vscsi->event = (struct virtio_scsi_event *)memlayout_directmap_phys_to_virt(vscsi->event_phys);
    memset(vscsi->event, 0, sizeof(*vscsi->event));
    struct virtqueue_buf evt_buf = { .addr = vscsi->event_phys, .len = sizeof(*vscsi->event), .write = 1 };
    uint16_t evt_head = 0;
    if (virtqueue_add_buf(vscsi->event_vq, &evt_buf, 1, &evt_head) != 0)
        goto err_remove;
    virtqueue_kick(vscsi->event_vq);

    if (virtscsi_scan(vscsi) == 0)
        return 0;

err_remove:
    virtscsi_remove(vdev);
    return -1;
}

static void virtscsi_remove(struct virtio_device *vdev)
{
    struct Scsi_Host *host;

    if (!vdev)
        return;
    struct virtio_scsi *vscsi = (struct virtio_scsi *)vdev->priv;
    if (!vscsi)
        return;
    virtio_reset_device(vdev);
    host = vscsi->host;
    if (host) {
        scsi_remove_host(host);
        scsi_host_put(host);
    }
    if (vscsi->event_phys)
        free_page(vscsi->event_phys);
    if (vdev->config && vdev->config->del_vqs)
        vdev->config->del_vqs(vdev);
    kfree(vscsi);
    vdev->priv = NULL;
}

static const struct virtio_device_id virtio_scsi_id_table[] = {
    { .device = VIRTIO_ID_SCSI, .vendor = VIRTIO_DEV_ANY_ID },
    { 0 }
};

static struct virtio_driver virtio_scsi_driver = {
    .driver = { .name = "virtio_scsi" },
    .id_table = virtio_scsi_id_table,
    .probe = virtscsi_probe,
    .remove = virtscsi_remove,
};

static int virtio_scsi_init(void)
{
    return register_virtio_driver(&virtio_scsi_driver);
}
module_init(virtio_scsi_init);
