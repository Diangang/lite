#include "linux/device.h"
#include "linux/init.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/list.h"
#include "linux/pci.h"
#include "linux/nvme.h"
#include "linux/vmalloc.h"
#include "linux/slab.h"
#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/errno.h"
#include "linux/time.h"
#include "linux/gfp.h"
#include "asm/pgtable.h"
#include "linux/pci_regs.h"
#include <stdint.h>

#include "nvme.h"

/*
 * Linux mapping: private queue state is owned by drivers/nvme/host/pci.c
 * instead of the shared nvme host header.
 */
struct nvme_queue {
    uint16_t qid;
    uint16_t depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t cq_phase;
    uint16_t next_cid;
    void *sq;
    void *cq;
    uint32_t sq_phys;
    uint32_t cq_phys;
    volatile uint32_t *sq_db;
    volatile uint32_t *cq_db;
};

struct nvme_dev_wrap {
    struct nvme_dev dev;
    struct nvme_queue admin_q;
    struct nvme_queue io_q;
};

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
#define NVME_CC_SHN_MASK    (3u << 14)
#define NVME_CC_IOSQES(v)   (((uint32_t)(v) & 0xF) << 16)      /* 6 => 64B */
#define NVME_CC_IOCQES(v)   (((uint32_t)(v) & 0xF) << 20)      /* 4 => 16B */

#define NVME_MAX_DEVS 4
#define ADMIN_TIMEOUT (60u * HZ)
#define NVME_IO_TIMEOUT (30u * HZ)
static uint8_t nvme_instance_used[NVME_MAX_DEVS];
static uint32_t nvme_cap_to_ticks(uint64_t cap);
static struct class nvme_class;

static int nvme_ctrl_register(struct nvme_dev *dev)
{
    if (!dev || !dev->pdev)
        return -1;
    if (dev->device)
        return 0;

    dev->device = device_create(&nvme_class, &dev->pdev->dev, 0, dev,
                                "%s", dev->name);
    if (!dev->device)
        return -1;
    return 0;
}

static void nvme_ctrl_unregister(struct nvme_dev *dev)
{
    if (!dev || !dev->device)
        return;
    device_unregister(dev->device);
    dev->device = NULL;
}

static int nvme_alloc_instance(void)
{
    for (int i = 0; i < NVME_MAX_DEVS; i++) {
        if (!nvme_instance_used[i]) {
            nvme_instance_used[i] = 1;
            return i;
        }
    }
    return -1;
}

static void nvme_release_instance(int instance)
{
    if (instance < 0 || instance >= NVME_MAX_DEVS)
        return;
    nvme_instance_used[instance] = 0;
}

static inline uint32_t mmio_read32(void *base, uint32_t off)
{
    return *(volatile uint32_t *)((uint32_t)base + off);
}

static inline void mmio_write32(void *base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)((uint32_t)base + off) = v;
}

/*
 * Ensure queue DMA writes are visible to the controller before ringing a doorbell.
 *
 * Linux mapping: NVMe host drivers use a DMA write memory barrier (wmb()) before
 * writing SQ/CQ doorbells to avoid the controller fetching partially written
 * commands/completions due to posted MMIO writes.
 */
static inline void nvme_wmb(void)
{
    __asm__ volatile("sfence" : : : "memory");
}

static inline void nvme_doorbell_write(volatile uint32_t *db, uint32_t v)
{
    if (!db)
        return;
    nvme_wmb();
    *db = v;
    /* Read-back to flush posted MMIO writes on PCIe. */
    (void)*db;
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

static int nvme_wait_ready(struct nvme_dev *dev, uint64_t cap, int enabled)
{
    uint32_t timeout_ticks = nvme_cap_to_ticks(cap);
    uint32_t deadline = time_get_jiffies() + timeout_ticks;
    while (1) {
        uint32_t csts = mmio_read32(dev->bar, NVME_REG_CSTS);
        int rdy = (csts & NVME_CSTS_RDY) != 0;
        if (rdy == enabled)
            return 0;
        if (time_before(deadline, time_get_jiffies()))
            return -1;
    }
}

static void nvme_dev_shutdown(struct nvme_dev *dev)
{
    uint64_t cap;

    if (!dev || !dev->bar)
        return;
    cap = dev->cap ? dev->cap : mmio_read64(dev->bar, NVME_REG_CAP);

    /* Linux mapping: nvme_disable_ctrl() clears SHN and EN from ctrl_config. */
    dev->ctrl_config &= ~NVME_CC_SHN_MASK;
    dev->ctrl_config &= ~NVME_CC_EN;
    mmio_write32(dev->bar, NVME_REG_CC, dev->ctrl_config);
    (void)nvme_wait_ready(dev, cap, 0);
}

static uint32_t nvme_cap_to_ticks(uint64_t cap)
{
    /* Linux mapping: timeout is (CAP.TO + 1) 500ms units. */
    uint32_t to = (uint32_t)((cap >> 24) & 0xFFu);
    return msecs_to_jiffies((to + 1u) * 500u);
}

static void nvme_queue_init_doorbells(struct nvme_dev *dev, struct nvme_queue *q)
{
    uint32_t base = 0x1000;
    uint32_t stride = dev->db_stride;
    uint32_t sq_off = base + (uint32_t)(2u * q->qid) * stride;
    uint32_t cq_off = base + (uint32_t)(2u * q->qid + 1u) * stride;
    q->sq_db = (volatile uint32_t *)((uint32_t)dev->bar + sq_off);
    q->cq_db = (volatile uint32_t *)((uint32_t)dev->bar + cq_off);
}

static uint16_t __nvme_submit_cmd(struct nvme_queue *q, const struct nvme_command *cmd)
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
    nvme_doorbell_write(q->sq_db, tail);
    return tmp.command_id;
}

static uint16_t nvme_submit_cmd(struct nvme_queue *q, const struct nvme_command *cmd)
{
    return __nvme_submit_cmd(q, cmd);
}

static int nvme_cq_poll_cid(struct nvme_queue *q, uint16_t cid, struct nvme_completion *cpl_out, uint32_t timeout_ticks)
{
    if (!q || cid == 0)
        return -EINVAL;
    volatile struct nvme_completion *cq = (volatile struct nvme_completion *)q->cq;

    /* Poll completion queue. */
    uint32_t deadline = time_get_jiffies() + timeout_ticks;
    while (1) {
        struct nvme_completion cpl = cq[q->cq_head];
        uint16_t phase = cpl.status & 1u;
        if (phase == q->cq_phase) {
            /*
             * Always consume CQEs in order (Linux mapping: nvme_process_cq()).
             *
             * Note: Completions can legitimately arrive out-of-order with respect
             * to submission. Even though Lite submits synchronously, stale CQEs
             * left behind would otherwise permanently poison the CQ head.
             */
            q->cq_head++;
            if (q->cq_head >= q->depth) {
                q->cq_head = 0;
                q->cq_phase ^= 1u;
            }
            nvme_doorbell_write(q->cq_db, q->cq_head);

            if (cpl.command_id == cid && cpl.sq_id == q->qid) {
                if (cpl_out)
                    *cpl_out = cpl;
                /* Status code is bits 15:1 (0 means success). */
                if ((cpl.status >> 1) != 0)
                    return -EIO;
                return 0;
            }
            /* Unrelated completion: keep polling until our CID completes. */
        }
        if (time_after_eq(time_get_jiffies(), deadline))
            return -ETIMEDOUT;
    }
}

static int __nvme_submit_sync_cmd(struct nvme_dev *dev, struct nvme_queue *q, struct nvme_command *cmd,
                                  struct nvme_completion *cpl_out, uint32_t timeout_ticks)
{
    if (!dev || !q || !cmd)
        return -EINVAL;
    uint16_t cid = nvme_submit_cmd(q, cmd);
    if (cid == 0)
        return -EINVAL;
    if (timeout_ticks == 0)
        timeout_ticks = ADMIN_TIMEOUT;
    return nvme_cq_poll_cid(q, cid, cpl_out, timeout_ticks);
}

static int nvme_submit_sync_admin_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
                                      struct nvme_completion *cpl_out)
{
    return __nvme_submit_sync_cmd(dev, dev->admin_q, cmd, cpl_out, ADMIN_TIMEOUT);
}

static int nvme_submit_sync_io_cmd(struct nvme_dev *dev, struct nvme_command *cmd,
                                   struct nvme_completion *cpl_out)
{
    return __nvme_submit_sync_cmd(dev, dev->io_q, cmd, cpl_out, NVME_IO_TIMEOUT);
}

static int nvme_identify(struct nvme_dev *dev, uint32_t nsid, uint32_t cns, void *buf, uint32_t buf_size)
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
    return nvme_submit_sync_admin_cmd(dev, &cmd, NULL);
}

static int nvme_identify_ns_list(struct nvme_dev *dev, void *buf, uint32_t buf_size)
{
    return nvme_identify(dev, 0, NVME_ID_CNS_ACTIVE_NS_LIST, buf, buf_size);
}

static int nvme_identify_ctrl(struct nvme_dev *dev, void *buf, uint32_t buf_size)
{
    return nvme_identify(dev, 0, NVME_ID_CNS_CTRL, buf, buf_size);
}

static int nvme_identify_ns(struct nvme_dev *dev, uint32_t nsid, void *buf, uint32_t buf_size)
{
    return nvme_identify(dev, nsid, NVME_ID_CNS_NS, buf, buf_size);
}

static int set_queue_count(struct nvme_dev *dev, int count)
{
    if (!dev || count <= 0)
        return -1;
    struct nvme_command cmd;
    struct nvme_completion cpl;
    memset(&cmd, 0, sizeof(cmd));
    memset(&cpl, 0, sizeof(cpl));
    cmd.opcode = NVME_ADMIN_OPC_SET_FEATURES;
    cmd.cdw10 = NVME_FEAT_NUM_QUEUES;
    cmd.cdw11 = (uint32_t)(count - 1) | ((uint32_t)(count - 1) << 16);
    int ret = nvme_submit_sync_admin_cmd(dev, &cmd, &cpl);
    if (ret != 0) {
        if ((cpl.status >> 1) != 0)
            return 0;
        return -1;
    }
    uint16_t ncqa = (uint16_t)((cpl.result & 0xFFFFu) + 1u);
    uint16_t nsqa = (uint16_t)(((cpl.result >> 16) & 0xFFFFu) + 1u);
    return (nsqa < ncqa) ? nsqa : ncqa;
}

static int nvme_create_io_queues(struct nvme_dev *dev)
{
    if (!dev)
        return -1;

    /* Linux flow: Set Features - Number of Queues, then create I/O CQ/SQ. */
    int queue_count = set_queue_count(dev, 1);
    if (queue_count <= 0)
        return -1;
    printf("nvme: num_queues allocated: %u\n", (unsigned)queue_count);
    /* Choose a small, safe depth. Must be <= (CAP.MQES + 1). */
    uint16_t max_depth = (uint16_t)(dev->mqes + 1u);
    uint16_t depth = 32;
    if (depth > max_depth)
        depth = max_depth;
    if (depth < 2)
        depth = 2;
    uint16_t qid = 1;

    memset(dev->io_q, 0, sizeof(*dev->io_q));
    dev->io_q->qid = qid;
    dev->io_q->depth = depth;
    dev->io_q->cq_phase = 1;
    dev->io_q->next_cid = 1;

    /*
     * NVMe requires ASQ/ACQ and I/O SQ/CQ to be physically contiguous and 4K aligned.
     * Lite does not have a full DMA mapping layer yet, so allocate one page per queue
     * (sufficient for our small depths) and use its physical address directly.
     */
    dev->io_q->sq = nvme_alloc_page(&dev->io_q->sq_phys);
    dev->io_q->cq = nvme_alloc_page(&dev->io_q->cq_phys);
    if (!dev->io_q->sq || !dev->io_q->cq)
        return -1;
    uint32_t cq_phys = dev->io_q->cq_phys;
    uint32_t sq_phys = dev->io_q->sq_phys;

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
    if (nvme_submit_sync_admin_cmd(dev, &cmd, &cpl) != 0) {
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
    nvme_doorbell_write(dbq.cq_db, 0);

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
    if (nvme_submit_sync_admin_cmd(dev, &cmd, &cpl) != 0) {
        printf("nvme: CREATE_IO_SQ failed (status=0x%x)\n", cpl.status);
        return -1;
    }

    nvme_queue_init_doorbells(dev, dev->io_q);
    return 0;
}

static int nvme_io_rw(struct nvme_ns *ns, int write, uint64_t lba, uint32_t nlb, void *buf, uint32_t buf_len)
{
    if (!ns || !ns->dev || !buf || nlb == 0)
        return -1;
    struct nvme_dev *dev = ns->dev;
    /*
     * PRP mapping:
     * - Prefer single-page PRP1 mappings.
     * - If the caller buffer crosses a page boundary, use a bounce buffer
     *   instead of PRP2 to avoid relying on multi-page PRP correctness.
     *
     * Linux mapping: the block layer may use bounce buffers when DMA mapping
     * constraints are not satisfied.
     */
    void *bounce = NULL;
    void *dma_buf = buf;
    uint32_t phys1 = virt_to_phys(dma_buf);
    if (phys1 == 0xFFFFFFFF)
        return -1;

    if (buf_len > 4096u)
        return -1;

    uint32_t off_in_page = phys1 & 0xFFFu;
    uint32_t pages = (off_in_page + buf_len + 4095u) / 4096u;
    if (pages > 1) {
        bounce = kmalloc(buf_len + 4096u);
        if (!bounce)
            return -1;
        dma_buf = (void *)((((uint32_t)bounce) + 4095u) & ~0xFFFu);
        if (write)
            memcpy(dma_buf, buf, buf_len);
        phys1 = virt_to_phys(dma_buf);
        if (phys1 == 0xFFFFFFFF) {
            kfree(bounce);
            return -1;
        }
        off_in_page = phys1 & 0xFFFu;
        if (off_in_page != 0) {
            kfree(bounce);
            return -1;
        }
    }
    uint64_t prp2 = 0;

    struct nvme_command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = write ? NVME_CMD_WRITE : NVME_CMD_READ;
    cmd.nsid = ns->ns_id;
    cmd.prp1 = (uint64_t)phys1;
    cmd.prp2 = prp2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (nlb - 1) & 0xFFFFu;
    struct nvme_completion cpl;
    memset(&cpl, 0, sizeof(cpl));
    int ret = nvme_submit_sync_io_cmd(dev, &cmd, &cpl);
    if (ret != 0) {
        /*
         * Linux mapping: request completion status is only meaningful if we
         * consumed a CQE for this command. Timeouts must not print a bogus
         * status value.
         */
        if (ret == -EIO) {
            printf("nvme: io %s failed lba=%u nlb=%u len=%u status=0x%x\n",
                   write ? "write" : "read",
                   (unsigned)lba, (unsigned)nlb, (unsigned)buf_len, (unsigned)cpl.status);
        } else {
            printf("nvme: io %s failed lba=%u nlb=%u len=%u err=%d\n",
                   write ? "write" : "read",
                   (unsigned)lba, (unsigned)nlb, (unsigned)buf_len, ret);
        }
        if (bounce)
            kfree(bounce);
        return -1;
    }
    if (bounce) {
        if (!write)
            memcpy(buf, dma_buf, buf_len);
        kfree(bounce);
    }
    return 0;
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

        uint32_t lba_size = 1u << ns->lba_shift;
        /* Simplified: require I/O aligned to namespace LBA size. */
        if ((offset % lba_size) != 0 || (bio->bi_size % lba_size) != 0) {
            bdput(bdev);
            blk_complete_request(q, rq, -1);
            continue;
        }

        uint64_t lba = offset / lba_size;
        uint32_t nlb = bio->bi_size / lba_size;
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

    dev->bar = ioremap((uint32_t)base, (uint32_t)size);
    if (dev->bar)
        printf("nvme: BAR0 mmio base=0x%x size=0x%x\n", (uint32_t)base, (uint32_t)size);
    return dev->bar ? 0 : -1;
}

static int nvme_dev_init(struct nvme_dev *dev)
{
    if (!dev)
        return -1;

    dev->page_size = 4096u;
    dev->cap = mmio_read64(dev->bar, NVME_REG_CAP);
    dev->mqes = (uint16_t)(dev->cap & 0xFFFFu);
    uint32_t dstrd = (uint32_t)((dev->cap >> 32) & 0xFu);
    dev->db_stride = 4u << dstrd;
    printf("nvme: CAP=0x%x%08x MQES=%u TO=%u DSTRD=%u\n",
           (uint32_t)(dev->cap >> 32), (uint32_t)dev->cap,
           (unsigned)(dev->mqes + 1u), (unsigned)((dev->cap >> 24) & 0xFFu), (unsigned)dstrd);

    /* Disable controller, then configure admin queue. */
    dev->ctrl_config = 0;
    mmio_write32(dev->bar, NVME_REG_CC, dev->ctrl_config);
    if (nvme_wait_ready(dev, dev->cap, 0) != 0)
        return -1;

    memset(dev->admin_q, 0, sizeof(*dev->admin_q));
    dev->admin_q->qid = 0;
    dev->admin_q->depth = 16;
    dev->admin_q->cq_phase = 1;
    dev->admin_q->next_cid = 1;
    dev->admin_q->sq = nvme_alloc_page(&dev->admin_q->sq_phys);
    dev->admin_q->cq = nvme_alloc_page(&dev->admin_q->cq_phys);
    if (!dev->admin_q->sq || !dev->admin_q->cq)
        return -1;
    uint32_t asq_phys = dev->admin_q->sq_phys;
    uint32_t acq_phys = dev->admin_q->cq_phys;

    uint32_t aqa = ((uint32_t)(dev->admin_q->depth - 1) << 16) | (uint32_t)(dev->admin_q->depth - 1);
    mmio_write32(dev->bar, NVME_REG_AQA, aqa);
    mmio_write64(dev->bar, NVME_REG_ASQ, (uint64_t)asq_phys);
    mmio_write64(dev->bar, NVME_REG_ACQ, (uint64_t)acq_phys);

    nvme_queue_init_doorbells(dev, dev->admin_q);

    /* Enable controller with default queue entry sizes. */
    dev->ctrl_config = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS(0) | NVME_CC_AMS_RR |
                       NVME_CC_SHN_NONE | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
    mmio_write32(dev->bar, NVME_REG_CC, dev->ctrl_config);
    if (nvme_wait_ready(dev, dev->cap, 1) != 0)
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
    if (nvme_identify_ns_list(dev, nslist, 4096) != 0) {
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

    if (nvme_identify_ctrl(dev, id, 4096) != 0) {
        free_page((unsigned long)id_phys);
        return -1;
    }
    memset(id, 0, 4096);
    if (nvme_identify_ns(dev, nsid, id, 4096) != 0) {
        free_page((unsigned long)id_phys);
        return -1;
    }

    uint64_t nsze = *(uint64_t *)((uint8_t *)id + 0x00);
    uint8_t flbas = *(uint8_t *)((uint8_t *)id + 0x1A);
    uint8_t fmt = flbas & 0x0Fu;
    uint8_t lbads = *(uint8_t *)((uint8_t *)id + 0x80 + (uint32_t)fmt * 16u + 2u);
    uint32_t lba_shift = lbads ? lbads : 9;
    uint32_t lba_size = 1u << lba_shift;
    uint64_t size_bytes = nsze * (uint64_t)lba_size;

    free_page((unsigned long)id_phys);

    struct nvme_ns *ns = kmalloc(sizeof(*ns));
    if (!ns)
        return -1;
    memset(ns, 0, sizeof(*ns));
    ns->dev = dev;
    ns->ns_id = nsid;
    ns->lba_shift = lba_shift;
    ns->size_bytes = size_bytes;

    struct request_queue *q = blk_init_queue(nvme_request_fn, ns);
    if (!q) {
        kfree(ns);
        return -1;
    }
    ns->queue = q;

    char disk_name[12];
    snprintf(disk_name, sizeof(disk_name), "%sn%u", dev->name, (unsigned)nsid);
    ns->disk = (struct gendisk *)kmalloc(sizeof(*ns->disk));
    if (!ns->disk) {
        blk_cleanup_queue(q);
        kfree(ns);
        return -1;
    }
    if (gendisk_init(ns->disk, disk_name, 259, dev->instance) != 0) {
        blk_cleanup_queue(q);
        kfree(ns->disk);
        kfree(ns);
        return -1;
    }
    ns->disk->queue = q;
    ns->disk->capacity = size_bytes / 512ULL;
    ns->disk->block_size = 1u << ns->lba_shift;
    ns->disk->private_data = ns;
    /* Linux mapping: namespace gendisk is parented by controller object (nvmeX). */
    if (!dev->device) {
        blk_cleanup_queue(ns->disk->queue);
        put_disk(ns->disk);
        kfree(ns);
        return -1;
    }
    ns->disk->parent = dev->device;
    if (add_disk(ns->disk) != 0) {
        blk_cleanup_queue(ns->disk->queue);
        put_disk(ns->disk);
        kfree(ns);
        return -1;
    }
    struct block_device *bdev = bdget_disk(ns->disk, 0);
    if (bdev) {
        bdev->size = size_bytes;
        bdev->block_size = 1u << ns->lba_shift;
        bdev->private_data = ns;
        bdput(bdev);
    }

    dev->ns = ns;
    return 0;
}

static void nvme_ns_remove(struct nvme_ns *ns)
{
    if (!ns)
        return;
    if (ns->disk) {
        if (ns->disk->dev)
            del_gendisk(ns->disk);
        if (ns->disk->queue)
            blk_cleanup_queue(ns->disk->queue);
        put_disk(ns->disk);
        ns->disk = NULL;
    }
    kfree(ns);
}

static void nvme_free_dev(struct nvme_dev *dev)
{
    if (!dev)
        return;
    if (dev->admin_q && dev->admin_q->sq_phys)
        free_page((unsigned long)dev->admin_q->sq_phys);
    if (dev->admin_q && dev->admin_q->cq_phys)
        free_page((unsigned long)dev->admin_q->cq_phys);
    if (dev->io_q && dev->io_q->sq_phys)
        free_page((unsigned long)dev->io_q->sq_phys);
    if (dev->io_q && dev->io_q->cq_phys)
        free_page((unsigned long)dev->io_q->cq_phys);
    if (dev->bar)
        iounmap(dev->bar);
    kfree(container_of(dev, struct nvme_dev_wrap, dev));
}

static const struct pci_device_id nvme_pci_ids[] = {
    /* Match NVMe controller class: base=0x01, sub=0x08, prog_if=any. */
    { .vendor = PCI_ANY_ID, .device = PCI_ANY_ID, .class = 0x010800, .class_mask = 0xFFFF00 },
    { 0 }
};

static int nvme_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    (void)id;
    if (!pdev)
        return -1;

    /* Ensure PCIe capability exists (Linux: NVMe is PCIe only). */
    if (!pcie_is_pcie(pdev)) {
        printf("nvme: missing PCIe capability\n");
        return -1;
    }

    struct nvme_dev_wrap *wrap = kmalloc(sizeof(*wrap));
    if (!wrap)
        return -1;
    memset(wrap, 0, sizeof(*wrap));
    struct nvme_dev *dev = &wrap->dev;
    dev->admin_q = &wrap->admin_q;
    dev->io_q = &wrap->io_q;
    dev->pdev = pdev;
    int instance = nvme_alloc_instance();
    if (instance < 0) {
        printf("nvme: no free instances\n");
        kfree(wrap);
        return -1;
    }
    dev->instance = (uint32_t)instance;
    snprintf(dev->name, sizeof(dev->name), "nvme%u", (unsigned)dev->instance);
    pdev->dev.driver_data = dev;

    if (nvme_map_mmio(dev) != 0) {
        printf("nvme: mmio map failed\n");
        pdev->dev.driver_data = NULL;
        nvme_release_instance(instance);
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_dev_init(dev) != 0) {
        printf("nvme: controller init failed\n");
        pdev->dev.driver_data = NULL;
        nvme_dev_shutdown(dev);
        nvme_release_instance(instance);
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_create_io_queues(dev) != 0) {
        printf("nvme: io queue init failed\n");
        pdev->dev.driver_data = NULL;
        nvme_dev_shutdown(dev);
        nvme_release_instance(instance);
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_ctrl_register(dev) != 0) {
        printf("nvme: controller device register failed\n");
        pdev->dev.driver_data = NULL;
        nvme_dev_shutdown(dev);
        nvme_release_instance(instance);
        nvme_free_dev(dev);
        return -1;
    }
    if (nvme_ns_init(dev) != 0) {
        printf("nvme: namespace init failed\n");
        nvme_dev_shutdown(dev);
        nvme_ctrl_unregister(dev);
        pdev->dev.driver_data = NULL;
        nvme_release_instance(instance);
        nvme_free_dev(dev);
        return -1;
    }
    printf("nvme: %sn1 ready\n", dev->name);
    return 0;
}

static void nvme_remove(struct pci_dev *pdev)
{
    struct nvme_dev *dev;

    if (!pdev)
        return;
    dev = (struct nvme_dev *)pdev->dev.driver_data;
    pdev->dev.driver_data = NULL;
    if (!dev)
        return;
    if (dev->ns) {
        nvme_ns_remove(dev->ns);
        dev->ns = NULL;
    }
    nvme_dev_shutdown(dev);
    nvme_ctrl_unregister(dev);
    nvme_release_instance((int)dev->instance);
    nvme_free_dev(dev);
}

static struct pci_driver nvme_driver = {
    .driver = { .name = "nvme" },
    .id_table = nvme_pci_ids,
    .probe = nvme_probe,
    .remove = nvme_remove,
};

static int nvme_init(void)
{
    memset(&nvme_class, 0, sizeof(nvme_class));
    nvme_class.name = "nvme";
    INIT_LIST_HEAD(&nvme_class.list);
    INIT_LIST_HEAD(&nvme_class.devices);
    if (class_register(&nvme_class) != 0)
        return -1;
    if (pci_register_driver(&nvme_driver) != 0) {
        class_unregister(&nvme_class);
        return -1;
    }
    return 0;
}
module_init(nvme_init);
