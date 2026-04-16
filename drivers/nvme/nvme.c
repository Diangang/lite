#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/pci.h"
#include "linux/pcie.h"
#include "linux/nvme.h"
#include "linux/vmalloc.h"
#include "linux/slab.h"
#include "linux/blkdev.h"
#include "linux/blk_queue.h"
#include "linux/blk_request.h"
#include "linux/bio.h"
#include "linux/timer.h"
#include "linux/time.h"
#include "linux/page_alloc.h"
#include "linux/memlayout.h"
#include "linux/pci_regs.h"
#include "asm/pgtable.h"
#include <stdint.h>

#include "nvme_internal.h"

/* NVMe controller registers (NVMe spec; offsets from BAR0) */
#define NVME_REG_CAP   0x00
#define NVME_REG_VS    0x08
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28 /* 64-bit: ASQ low/high */
#define NVME_REG_ACQ   0x30 /* 64-bit: ACQ low/high */

#define NVME_CSTS_RDY (1u << 0)

/* CC fields (NVMe spec) */
#define NVME_CC_EN          (1u << 0)
#define NVME_CC_CSS_NVM     (0u << 4)
#define NVME_CC_MPS(mps)    (((uint32_t)(mps) & 0xF) << 7)     /* MPS = log2(page_size)-12 */
#define NVME_CC_AMS_RR      (0u << 11)
#define NVME_CC_SHN_NONE    (0u << 14)
#define NVME_CC_IOSQES(v)   (((uint32_t)(v) & 0xF) << 16)      /* 6 => 64B */
#define NVME_CC_IOCQES(v)   (((uint32_t)(v) & 0xF) << 20)      /* 4 => 16B */

#define NVME_MAX_DEVS 4
static struct nvme_dev *nvme_devs[NVME_MAX_DEVS];
static struct nvme_ns *nvme_ns_map[NVME_MAX_DEVS];

static int nvme_alloc_instance(void)
{
    for (int i = 0; i < NVME_MAX_DEVS; i++) {
        if (!nvme_devs[i] && !nvme_ns_map[i])
            return i;
    }
    return -1;
}

static int nvme_find_instance_by_pdev(struct pci_dev *pdev)
{
    if (!pdev)
        return -1;
    for (int i = 0; i < NVME_MAX_DEVS; i++) {
        if (nvme_devs[i] && nvme_devs[i]->pdev == pdev)
            return i;
    }
    return -1;
}

static void nvme_make_disk_name(char *name, uint32_t instance)
{
    if (!name)
        return;
    /* Support up to nvme9n1 in this minimal implementation. */
    name[0] = 'n';
    name[1] = 'v';
    name[2] = 'm';
    name[3] = 'e';
    name[4] = (char)('0' + (instance % 10));
    name[5] = 'n';
    name[6] = '1';
    name[7] = 0;
}

static inline uint32_t mmio_read32(void *base, uint32_t off)
{
    return *(volatile uint32_t *)((uint32_t)base + off);
}

static inline void mmio_write32(void *base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)((uint32_t)base + off) = v;
}

static inline uint64_t mmio_read64(void *base, uint32_t off)
{
    uint64_t lo = mmio_read32(base, off);
    uint64_t hi = mmio_read32(base, off + 4);
    return lo | (hi << 32);
}

static inline void mmio_write64(void *base, uint32_t off, uint64_t v)
{
    mmio_write32(base, off, (uint32_t)(v & 0xFFFFFFFFu));
    mmio_write32(base, off + 4, (uint32_t)(v >> 32));
}

static void *nvme_alloc_page(uint32_t *phys_out)
{
    void *phys = alloc_page(GFP_KERNEL);
    if (!phys)
        return NULL;
    uint32_t p = (uint32_t)phys;
    if (phys_out)
        *phys_out = p;
    void *v = memlayout_directmap_phys_to_virt(p);
    memset(v, 0, 4096);
    return v;
}

static int nvme_wait_csts_rdy(struct nvme_dev *dev, int want_rdy, uint32_t timeout_ticks)
{
    uint32_t start = timer_get_ticks();
    while (1) {
        uint32_t csts = mmio_read32(dev->mmio, NVME_REG_CSTS);
        int rdy = (csts & NVME_CSTS_RDY) != 0;
        if (rdy == want_rdy)
            return 0;
        if ((timer_get_ticks() - start) > timeout_ticks)
            return -1;
    }
}

static uint32_t nvme_cap_to_ticks(uint64_t cap)
{
    /* CAP.TO is in 500ms units (NVMe spec). Map to jiffies (HZ=100). */
    uint32_t to = (uint32_t)((cap >> 24) & 0xFFu);
    if (to == 0)
        to = 1;
    uint32_t ticks_per_500ms = (HZ / 2);
    if (ticks_per_500ms == 0)
        ticks_per_500ms = 1;
    return to * ticks_per_500ms;
}

static void nvme_queue_init_doorbells(struct nvme_dev *dev, struct nvme_queue *q)
{
    uint32_t base = 0x1000;
    uint32_t stride = dev->db_stride;
    uint32_t sq_off = base + (uint32_t)(2u * q->qid) * stride;
    uint32_t cq_off = base + (uint32_t)(2u * q->qid + 1u) * stride;
    q->sq_db = (volatile uint32_t *)((uint32_t)dev->mmio + sq_off);
    q->cq_db = (volatile uint32_t *)((uint32_t)dev->mmio + cq_off);
}

static uint16_t nvme_sq_submit(struct nvme_queue *q, const struct nvme_command *cmd)
{
    if (!q || !cmd)
        return 0;
    /*
     * SQ/CQ are DMA-visible to the controller. Treat them as volatile and insert a
     * compiler barrier before ringing doorbells, otherwise -O2 may reorder/cache
     * loads/stores and we can miss completions or submit half-written commands.
     */
    volatile struct nvme_command *sq = (volatile struct nvme_command *)q->sq;

    struct nvme_command tmp = *cmd;
    tmp.command_id = q->next_cid++;
    if (q->next_cid == 0)
        q->next_cid = 1;

    uint16_t tail = q->sq_tail;
    sq[tail] = tmp;
    tail++;
    if (tail >= q->depth)
        tail = 0;
    q->sq_tail = tail;
    __asm__ volatile ("" : : : "memory");
    *q->sq_db = tail;
    return tmp.command_id;
}

static int nvme_cq_poll_cid(struct nvme_queue *q, uint16_t cid, struct nvme_completion *cpl_out, uint32_t timeout_ticks)
{
    if (!q || cid == 0)
        return -1;
    volatile struct nvme_completion *cq = (volatile struct nvme_completion *)q->cq;

    /* Poll completion queue. */
    uint32_t start = timer_get_ticks();
    while (1) {
        struct nvme_completion cpl = cq[q->cq_head];
        uint16_t phase = cpl.status & 1u;
        if (phase == q->cq_phase) {
            if (cpl.command_id != cid || cpl.sq_id != q->qid)
                return -1;
            if (cpl_out)
                *cpl_out = cpl;
            q->cq_head++;
            if (q->cq_head >= q->depth) {
                q->cq_head = 0;
                q->cq_phase ^= 1u;
            }
            *q->cq_db = q->cq_head;
            /* Status code is bits 15:1 (0 means success). */
            if ((cpl.status >> 1) != 0)
                return -1;
            return 0;
        }
        if ((timer_get_ticks() - start) > timeout_ticks)
            return -1;
    }
}

static int nvme_submit_cmd(struct nvme_queue *q, struct nvme_command *cmd, struct nvme_completion *cpl_out)
{
    if (!q || !cmd)
        return -1;
    /*
     * Linux mapping:
     * - submission and completion are separate concerns (irq-driven or polling fallback).
     * Keep the API shape split so Stage 3+ can plug an interrupt completion path later.
     */
    uint16_t cid = nvme_sq_submit(q, cmd);
    if (cid == 0)
        return -1;
    return nvme_cq_poll_cid(q, cid, cpl_out, 500);
}

static int nvme_admin_identify(struct nvme_dev *dev, uint32_t nsid, uint32_t cns, void *buf, uint32_t buf_size)
{
    if (!dev || !buf || buf_size < 4096)
        return -1;
    uint32_t phys = virt_to_phys(buf);
    if (phys == 0xFFFFFFFF)
        return -1;

    struct nvme_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OPC_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)phys;
    cmd.cdw10 = cns & 0xFF;
    return nvme_submit_cmd(&dev->admin_q, &cmd, NULL);
}

static int nvme_admin_set_features_num_queues(struct nvme_dev *dev, uint16_t nsq, uint16_t ncq,
                                              uint16_t *nsq_alloc, uint16_t *ncq_alloc)
{
    if (!dev || nsq == 0 || ncq == 0)
        return -1;
    struct nvme_command cmd;
    struct nvme_completion cpl;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OPC_SET_FEATURES;
    cmd.cdw10 = NVME_FEAT_NUM_QUEUES;
    /* CDW11: [31:16] NSQR (0-based), [15:0] NCQR (0-based) */
    cmd.cdw11 = ((uint32_t)(nsq - 1) << 16) | (uint32_t)(ncq - 1);
    if (nvme_submit_cmd(&dev->admin_q, &cmd, &cpl) != 0)
        return -1;
    uint16_t ncqa = (uint16_t)(cpl.result & 0xFFFFu);
    uint16_t nsqa = (uint16_t)((cpl.result >> 16) & 0xFFFFu);
    if (nsq_alloc)
        *nsq_alloc = (uint16_t)(nsqa + 1u);
    if (ncq_alloc)
        *ncq_alloc = (uint16_t)(ncqa + 1u);
    return 0;
}

static int nvme_create_io_queues(struct nvme_dev *dev)
{
    if (!dev)
        return -1;

    /* Linux flow: Set Features - Number of Queues, then create I/O CQ/SQ. */
    uint16_t nsq_alloc = 0, ncq_alloc = 0;
    if (nvme_admin_set_features_num_queues(dev, 1, 1, &nsq_alloc, &ncq_alloc) != 0) {
        printf("nvme: Set Features (Number of Queues) failed, continuing\n");
    } else {
        printf("nvme: num_queues allocated: NSQ=%u NCQ=%u\n", (unsigned)nsq_alloc, (unsigned)ncq_alloc);
    }

    /* Choose a small, safe depth. Must be <= (CAP.MQES + 1). */
    uint16_t max_depth = (uint16_t)(dev->mqes + 1u);
    uint16_t depth = 32;
    if (depth > max_depth)
        depth = max_depth;
    if (depth < 2)
        depth = 2;
    uint16_t qid = 1;

    memset(&dev->io_q, 0, sizeof(dev->io_q));
    dev->io_q.qid = qid;
    dev->io_q.depth = depth;
    dev->io_q.cq_phase = 1;
    dev->io_q.next_cid = 1;

    /*
     * NVMe requires ASQ/ACQ and I/O SQ/CQ to be physically contiguous and 4K aligned.
     * Lite does not have a full DMA mapping layer yet, so allocate one page per queue
     * (sufficient for our small depths) and use its physical address directly.
     */
    dev->io_q.sq = nvme_alloc_page(&dev->io_q.sq_phys);
    dev->io_q.cq = nvme_alloc_page(&dev->io_q.cq_phys);
    if (!dev->io_q.sq || !dev->io_q.cq)
        return -1;
    uint32_t cq_phys = dev->io_q.cq_phys;
    uint32_t sq_phys = dev->io_q.sq_phys;

    /* Create I/O CQ (admin cmd). */
    struct nvme_command cmd;
    struct nvme_completion cpl;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OPC_CREATE_IO_CQ;
    cmd.prp1 = (uint64_t)cq_phys;
    cmd.cdw10 = (uint32_t)qid | ((uint32_t)(depth - 1) << 16);
    /*
     * NVMe Create I/O CQ CDW11 (see industry-standard nvme headers, e.g. EDK2 Nvme.h):
     * - bits 31:16 IV
     * - bit 1     IEN
     * - bit 0     PC
     *
     * We poll completions, so IEN=0, IV=0.
     */
    cmd.cdw11 = (0u << 16) | (0u << 1) | 1u;
    if (nvme_submit_cmd(&dev->admin_q, &cmd, &cpl) != 0) {
        printf("nvme: CREATE_IO_CQ failed (status=0x%x)\n", cpl.status);
        return -1;
    }
    /*
     * Some controllers expect the CQ head doorbell to be initialized after the CQ is
     * created, before an SQ references it.
     */
    struct nvme_queue dbq;
    memset(&dbq, 0, sizeof(dbq));
    dbq.qid = qid;
    nvme_queue_init_doorbells(dev, &dbq);
    *dbq.cq_db = 0;

    /* Create I/O SQ (admin cmd). */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_OPC_CREATE_IO_SQ;
    cmd.prp1 = (uint64_t)sq_phys;
    cmd.cdw10 = (uint32_t)qid | ((uint32_t)(depth - 1) << 16);
    /*
     * NVMe Create I/O SQ CDW11 (see industry-standard nvme headers, e.g. EDK2 Nvme.h):
     * - bits 31:16 CQID
     * - bits 2:1   QPRIO
     * - bit 0      PC
     */
    cmd.cdw11 = ((uint32_t)qid << 16) | (0u << 1) | 1u;
    if (nvme_submit_cmd(&dev->admin_q, &cmd, &cpl) != 0) {
        printf("nvme: CREATE_IO_SQ failed (status=0x%x)\n", cpl.status);
        return -1;
    }

    nvme_queue_init_doorbells(dev, &dev->io_q);
    return 0;
}

static int nvme_io_rw(struct nvme_ns *ns, int write, uint64_t lba, uint32_t nlb, void *buf, uint32_t buf_len)
{
    if (!ns || !ns->dev || !buf || nlb == 0)
        return -1;
    struct nvme_dev *dev = ns->dev;
    uint32_t phys1 = virt_to_phys(buf);
    if (phys1 == 0xFFFFFFFF)
        return -1;

    /* Simplified PRP mapping: support up to 2 pages without PRP list. */
    uint32_t off_in_page = phys1 & 0xFFF;
    uint32_t pages = (off_in_page + buf_len + 4095u) / 4096u;
    uint64_t prp2 = 0;
    if (pages > 2)
        return -1;
    if (pages == 2) {
        uint32_t next_page_va = ((uint32_t)buf & ~0xFFFu) + 4096u;
        uint32_t phys2 = virt_to_phys((void *)next_page_va);
        if (phys2 == 0xFFFFFFFF)
            return -1;
        prp2 = (uint64_t)phys2;
    }

    struct nvme_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = ns->nsid;
    cmd.prp1 = (uint64_t)phys1;
    cmd.prp2 = prp2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (nlb - 1) & 0xFFFFu;
    return nvme_submit_cmd(&dev->io_q, &cmd, NULL);
}

static void nvme_request_fn(struct request_queue *q)
{
    if (!q)
        return;
    struct nvme_ns *ns = (struct nvme_ns *)q->queuedata;
    while (1) {
        struct request *rq = blk_fetch_request(q);
        if (!rq)
            break;
        struct bio *bio = rq->bio;
        if (!bio || !bio->bi_bdev || !bio->bi_buf || bio->bi_size == 0) {
            blk_complete_request(q, rq, -1);
            continue;
        }

        uint32_t offset = (uint32_t)bio->bi_sector * 512u + bio->bi_byte_offset;
        struct block_device *bdev = ns->disk ? bdget_disk(ns->disk, 0) : NULL;
        if (!bdev || offset + bio->bi_size > bdev->size) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }

        /* Simplified: require I/O aligned to namespace LBA size. */
        if ((offset % ns->lba_size) != 0 || (bio->bi_size % ns->lba_size) != 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }

        uint64_t lba = offset / ns->lba_size;
        uint32_t nlb = bio->bi_size / ns->lba_size;
        int is_write = (bio->bi_opf == REQ_OP_WRITE);
        if (nvme_io_rw(ns, is_write, lba, nlb, bio->bi_buf, bio->bi_size) != 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }
        block_account_io(bdev, is_write, bio->bi_size);
        bdput(bdev);
        blk_complete_request(q, rq, 0);
    }
}

static int nvme_map_mmio(struct nvme_dev *dev)
{
    if (!dev || !dev->pdev)
        return -1;

    /* BAR0 is expected to be NVMe controller registers. */
    uint64_t base = 0;
    uint64_t size = 0;
    if (dev->pdev->bar[0] & 0x1)
        return -1;
    if (((dev->pdev->bar[0] >> 1) & 0x3) == 2) {
        base = ((uint64_t)dev->pdev->bar[1] << 32) | (dev->pdev->bar[0] & ~0xFu);
        size = ((uint64_t)dev->pdev->bar_size[1] << 32) | (uint64_t)dev->pdev->bar_size[0];
    } else {
        base = (uint64_t)(dev->pdev->bar[0] & ~0xFu);
        size = (uint64_t)dev->pdev->bar_size[0];
    }
    if (base == 0 || size == 0)
        return -1;

    /* Lite is 32-bit; require MMIO below 4G. */
    if (base > 0xFFFFFFFFu)
        return -1;
    if (size > 0x100000u)
        size = 0x100000u; /* map 1MB max for safety */

    dev->mmio = ioremap((uint32_t)base, (uint32_t)size);
    if (dev->mmio)
        printf("nvme: BAR0 mmio base=0x%x size=0x%x\n", (uint32_t)base, (uint32_t)size);
    return dev->mmio ? 0 : -1;
}

static int nvme_dev_init(struct nvme_dev *dev)
{
    if (!dev)
        return -1;

    dev->cap = mmio_read64(dev->mmio, NVME_REG_CAP);
    dev->vs = mmio_read32(dev->mmio, NVME_REG_VS);
    dev->mqes = (uint16_t)(dev->cap & 0xFFFFu);
    dev->cqr = (uint8_t)((dev->cap >> 16) & 0x1u);
    uint32_t dstrd = (uint32_t)((dev->cap >> 32) & 0xFu);
    dev->db_stride = 4u << dstrd;
    uint32_t to_ticks = nvme_cap_to_ticks(dev->cap);
    printf("nvme: CAP=0x%x%08x MQES=%u TO=%u DSTRD=%u\n",
           (uint32_t)(dev->cap >> 32), (uint32_t)dev->cap,
           (unsigned)(dev->mqes + 1u), (unsigned)((dev->cap >> 24) & 0xFFu), (unsigned)dstrd);

    /* Disable controller, then configure admin queue. */
    mmio_write32(dev->mmio, NVME_REG_CC, 0);
    if (nvme_wait_csts_rdy(dev, 0, to_ticks) != 0)
        return -1;

    memset(&dev->admin_q, 0, sizeof(dev->admin_q));
    dev->admin_q.qid = 0;
    dev->admin_q.depth = 16;
    dev->admin_q.cq_phase = 1;
    dev->admin_q.next_cid = 1;
    dev->admin_q.sq = nvme_alloc_page(&dev->admin_q.sq_phys);
    dev->admin_q.cq = nvme_alloc_page(&dev->admin_q.cq_phys);
    if (!dev->admin_q.sq || !dev->admin_q.cq)
        return -1;
    uint32_t asq_phys = dev->admin_q.sq_phys;
    uint32_t acq_phys = dev->admin_q.cq_phys;

    uint32_t aqa = ((uint32_t)(dev->admin_q.depth - 1) << 16) | (uint32_t)(dev->admin_q.depth - 1);
    mmio_write32(dev->mmio, NVME_REG_AQA, aqa);
    mmio_write64(dev->mmio, NVME_REG_ASQ, (uint64_t)asq_phys);
    mmio_write64(dev->mmio, NVME_REG_ACQ, (uint64_t)acq_phys);

    nvme_queue_init_doorbells(dev, &dev->admin_q);

    /* Enable controller with default queue entry sizes. */
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS(0) | NVME_CC_AMS_RR |
                  NVME_CC_SHN_NONE | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
    mmio_write32(dev->mmio, NVME_REG_CC, cc);
    if (nvme_wait_csts_rdy(dev, 1, to_ticks) != 0)
        return -1;

    return 0;
}

static int nvme_ns_init(struct nvme_dev *dev)
{
    if (!dev)
        return -1;

    /* Identify active namespace list (CNS=2). */
    uint32_t nslist_phys = 0;
    void *nslist = nvme_alloc_page(&nslist_phys);
    if (!nslist || !nslist_phys)
        return -1;
    if (nvme_admin_identify(dev, 0, NVME_ID_CNS_ACTIVE_NS_LIST, nslist, 4096) != 0) {
        free_page((unsigned long)nslist_phys);
        return -1;
    }
    uint32_t nsid = *(uint32_t *)nslist;
    free_page((unsigned long)nslist_phys);
    if (nsid == 0) {
        printf("nvme: no active namespaces\n");
        return -1;
    }

    uint32_t id_phys = 0;
    void *id = nvme_alloc_page(&id_phys);
    if (!id || !id_phys)
        return -1;
    memset(id, 0, 4096);

    if (nvme_admin_identify(dev, 0, NVME_ID_CNS_CTRL, id, 4096) != 0) {
        free_page((unsigned long)id_phys);
        return -1;
    }
    memset(id, 0, 4096);
    if (nvme_admin_identify(dev, nsid, NVME_ID_CNS_NS, id, 4096) != 0) {
        free_page((unsigned long)id_phys);
        return -1;
    }

    uint64_t nsze = *(uint64_t *)((uint8_t *)id + 0x00);
    uint8_t flbas = *(uint8_t *)((uint8_t *)id + 0x1A);
    uint8_t fmt = flbas & 0x0Fu;
    uint8_t lbads = *(uint8_t *)((uint8_t *)id + 0x80 + (uint32_t)fmt * 16u + 2u);
    uint32_t lba_size = 1u << lbads;
    uint64_t size_bytes = nsze * (uint64_t)lba_size;

    free_page((unsigned long)id_phys);

    struct nvme_ns *ns = kmalloc(sizeof(*ns));
    if (!ns)
        return -1;
    memset(ns, 0, sizeof(*ns));
    ns->dev = dev;
    ns->instance = dev->instance;
    ns->nsid = nsid;
    ns->lba_size = lba_size ? lba_size : 512;
    ns->size_bytes = size_bytes;

    struct request_queue *q = blk_init_queue(nvme_request_fn, ns);
    if (!q) {
        kfree(ns);
        return -1;
    }
    ns->queue = q;

    char disk_name[8];
    nvme_make_disk_name(disk_name, ns->instance);
    ns->disk = (struct gendisk *)kmalloc(sizeof(*ns->disk));
    if (!ns->disk) {
        blk_cleanup_queue(q);
        kfree(ns);
        return -1;
    }
    if (gendisk_init(ns->disk, disk_name, 259, ns->instance) != 0) {
        blk_cleanup_queue(q);
        kfree(ns->disk);
        kfree(ns);
        return -1;
    }
    ns->disk->queue = q;
    ns->disk->capacity = size_bytes / 512ULL;
    ns->disk->block_size = ns->lba_size;
    ns->disk->private_data = ns;
    /* Linux mapping: namespace gendisk is parented by controller object (nvmeX). */
    if (!dev->ctrl_registered)
        return -1;
    ns->disk->parent = &dev->ctrl_dev;
    if (add_disk(ns->disk) != 0) {
        blk_cleanup_queue(ns->disk->queue);
        kfree(ns->disk);
        kfree(ns);
        return -1;
    }
    struct block_device *bdev = bdget_disk(ns->disk, 0);
    if (bdev) {
        bdev->size = size_bytes;
        bdev->block_size = ns->lba_size;
        bdev->private_data = ns;
        bdput(bdev);
    }

    nvme_ns_map[ns->instance] = ns;
    return 0;
}

static void nvme_free_dev(struct nvme_dev *dev)
{
    if (!dev)
        return;
    if (dev->admin_q.sq_phys)
        free_page((unsigned long)dev->admin_q.sq_phys);
    if (dev->admin_q.cq_phys)
        free_page((unsigned long)dev->admin_q.cq_phys);
    if (dev->io_q.sq_phys)
        free_page((unsigned long)dev->io_q.sq_phys);
    if (dev->io_q.cq_phys)
        free_page((unsigned long)dev->io_q.cq_phys);
    if (dev->mmio)
        iounmap(dev->mmio);
    kfree(dev);
}

static const struct pci_device_id nvme_pci_ids[] = {
    /* Match NVMe controller class: base=0x01, sub=0x08, prog_if=any. */
    { .vendor = PCI_ANY_ID, .device = PCI_ANY_ID, .class = 0x010800, .class_mask = 0xFFFF00 },
    { 0 }
};

static int nvme_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    (void)id;
    if (!pdev)
        return -1;

    /* Ensure PCIe capability exists (Linux: NVMe is PCIe only). */
    if (!pcie_is_pcie(pdev)) {
        printf("nvme: missing PCIe capability\n");
        return -1;
    }

    struct nvme_dev *dev = kmalloc(sizeof(*dev));
    if (!dev)
        return -1;
    memset(dev, 0, sizeof(*dev));
    dev->pdev = pdev;
    int instance = nvme_alloc_instance();
    if (instance < 0) {
        printf("nvme: no free instances\n");
        kfree(dev);
        return -1;
    }
    dev->instance = (uint32_t)instance;

    if (nvme_map_mmio(dev) != 0) {
        printf("nvme: mmio map failed\n");
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_dev_init(dev) != 0) {
        printf("nvme: controller init failed\n");
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_create_io_queues(dev) != 0) {
        printf("nvme: io queue init failed\n");
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_ctrl_register(dev) != 0) {
        printf("nvme: controller device register failed\n");
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_ns_init(dev) != 0) {
        printf("nvme: namespace init failed\n");
        nvme_ctrl_unregister(dev);
        nvme_free_dev(dev);
        return -1;
    }

    nvme_devs[instance] = dev;
    printf("nvme: nvme%dn1 ready\n", (int)dev->instance);
    return 0;
}

static void nvme_pci_remove(struct pci_dev *pdev)
{
    int instance = nvme_find_instance_by_pdev(pdev);
    if (instance < 0)
        return;
    if (nvme_ns_map[instance]) {
        if (nvme_ns_map[instance]->disk && nvme_ns_map[instance]->disk->dev)
            del_gendisk(nvme_ns_map[instance]->disk);
        if (nvme_ns_map[instance]->disk && nvme_ns_map[instance]->disk->queue)
            blk_cleanup_queue(nvme_ns_map[instance]->disk->queue);
        if (nvme_ns_map[instance]->disk)
            kfree(nvme_ns_map[instance]->disk);
        kfree(nvme_ns_map[instance]);
        nvme_ns_map[instance] = NULL;
    }
    if (nvme_devs[instance]) {
        nvme_ctrl_unregister(nvme_devs[instance]);
        nvme_free_dev(nvme_devs[instance]);
        nvme_devs[instance] = NULL;
    }
}

static struct pci_driver nvme_pci_driver = {
    .name = "nvme",
    .id_table = nvme_pci_ids,
    .probe = nvme_pci_probe,
    .remove = nvme_pci_remove,
};

static int nvme_driver_init(void)
{
    return pci_register_driver(&nvme_pci_driver);
}
module_init(nvme_driver_init);
