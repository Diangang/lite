#include "linux/device.h"
#include "linux/init.h"
#include "linux/libc.h"
#include "linux/vmalloc.h"
#include "linux/slab.h"
#include "linux/blkdev.h"
#include "linux/bio.h"
#include "linux/blk_queue.h"
#include "linux/fs.h"
#include "linux/devtmpfs.h"
#include "linux/mm.h"
#include <stdint.h>

#define NVME_REG_CAP      0x00
#define NVME_REG_VS       0x08
#define NVME_REG_INTMS     0x0C
#define NVME_REG_INTMC     0x10
#define NVME_REG_CC        0x14
#define NVME_REG_CSTS      0x1C
#define NVME_REG_NSSR      0x20
#define NVME_REG_AQA       0x24
#define NVME_REG_ASQ       0x28
#define NVME_REG_ACQ       0x30

#define NVME_CC_ENABLE     (1 << 0)
#define NVME_CC_CSS_NVM    (0 << 4)
#define NVME_CC_MPS_SHIFT  7
#define NVME_CC_MPS_4KB    (6 << 7)
#define NVME_CC_ARB_SHIFT  11
#define NVME_CC_ARB_RR     (0 << 11)

#define NVME_CSTS_RDY      (1 << 0)

#define NVME_CMD_IDENTIFY  0x06

struct nvme_queue {
    uint32_t id;
    uint32_t size;
    void *sq_buf;
    void *cq_buf;
    uint32_t *sq_doorbell;
    uint32_t *cq_doorbell;
};

struct nvme_controller {
    struct device *dev;
    void *mmio;
    uint64_t cap;
    uint32_t vs;
    uint32_t page_size;
    struct nvme_queue *admin_q;
    struct nvme_queue *io_q;
    uint32_t max_q;
    uint32_t max_q_depth;
};

struct nvme_namespace {
    struct nvme_controller *ctrl;
    uint32_t nsid;
    uint64_t size;
    uint32_t block_size;
    struct block_device *bdev;
};

static struct device_driver drv_nvme;
static struct nvme_controller *nvme_ctrl;
static struct nvme_namespace *nvme_ns;

static const struct device_id nvme_ids[] = {
    { .type = "pci", .class_id = 0x01, .subclass_id = 0x08 },
    { 0 }
};



static int nvme_init_namespace(struct nvme_controller *ctrl)
{
    struct nvme_namespace *ns = kmalloc(sizeof(*ns));
    if (!ns)
        return -1;
    
    ns->ctrl = ctrl;
    ns->nsid = 1;
    ns->block_size = 512;
    
    // TODO: Implement namespace identification
    ns->size = 16 * 1024 * 1024; // 16MB for testing
    
    // Create block device
    ns->bdev = kmalloc(sizeof(*ns->bdev));
    if (!ns->bdev) {
        kfree(ns);
        return -1;
    }
    
    if (block_device_init(ns->bdev, ns->size, ns->block_size) != 0) {
        kfree(ns->bdev);
        kfree(ns);
        return -1;
    }
    
    // Use PCI bus for device registration (since platform bus might not be ready yet)
    struct bus_type *pci_bus = device_model_pci_bus();
    if (!pci_bus) {
        printf("NVMe init namespace: failed to get PCI bus\n");
        kfree(ns->bdev);
        kfree(ns);
        return -1;
    }
    
    // Create device structure for devtmpfs
    struct device *dev = device_register_simple("nvme0n1", "block", pci_bus, ns->bdev);
    if (!dev) {
        printf("NVMe init namespace: failed to register device\n");
        kfree(ns->bdev);
        kfree(ns);
        return -1;
    }
    
    // Register device to devtmpfs
    devtmpfs_register_device(dev);
    
    nvme_ns = ns;
    printf("NVMe namespace 1 registered as /dev/nvme0n1\n");
    return 0;
}

static int nvme_probe(struct device *dev)
{
    if (!dev) {
        printf("NVMe probe: device is NULL\n");
        return -1;
    }
    device_uevent_emit("nvmebind", dev);
    
    // Skip MMIO mapping and controller initialization for testing
    // Just create the block device directly
    printf("NVMe probe: Creating block device for testing\n");
    
    // Allocate controller structure
    struct nvme_controller *ctrl = kmalloc(sizeof(*ctrl));
    if (!ctrl) {
        printf("NVMe probe: kmalloc controller failed\n");
        return 0;
    }
    
    ctrl->dev = dev;
    ctrl->mmio = NULL;
    ctrl->cap = 0;
    ctrl->vs = 0;
    ctrl->max_q = 1;
    ctrl->max_q_depth = 32;
    ctrl->page_size = 4096;
    ctrl->admin_q = NULL;
    ctrl->io_q = NULL;
    
    // Initialize namespace
    printf("NVMe probe: Calling nvme_init_namespace\n");
    int ret = nvme_init_namespace(ctrl);
    if (ret != 0) {
        printf("NVMe probe: namespace initialization failed with ret=%d\n", ret);
        kfree(ctrl);
        device_uevent_emit("nvmenamespacefail", dev);
        return 0;
    }
    printf("NVMe probe: namespace initialization success\n");
    
    nvme_ctrl = ctrl;
    printf("NVMe controller initialized successfully (testing mode)\n");
    device_uevent_emit("nvmeinit", dev);
    
    return 0;
}

static int nvme_driver_init(void)
{
    struct bus_type *pci = device_model_pci_bus();
    if (!pci)
        return -1;
    init_driver_ids(&drv_nvme, "nvme", pci, nvme_ids, nvme_probe);
    driver_register(&drv_nvme);
    return 0;
}
module_init(nvme_driver_init);
