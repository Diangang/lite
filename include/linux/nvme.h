/*
 * Minimal NVMe definitions aligned to Linux naming.
 *
 * This is a small subset required by drivers/nvme/nvme.c. It is not a complete
 * NVMe spec implementation.
 */
#ifndef LINUX_NVME_H
#define LINUX_NVME_H

#include <stdint.h>

/* Admin command opcodes (NVMe spec; Linux: include/linux/nvme.h) */
#define NVME_ADMIN_OPC_DELETE_IO_SQ 0x00
#define NVME_ADMIN_OPC_CREATE_IO_SQ 0x01
#define NVME_ADMIN_OPC_GET_LOG_PAGE 0x02
#define NVME_ADMIN_OPC_DELETE_IO_CQ 0x04
#define NVME_ADMIN_OPC_CREATE_IO_CQ 0x05
#define NVME_ADMIN_OPC_IDENTIFY     0x06
#define NVME_ADMIN_OPC_SET_FEATURES 0x09

/* Feature identifiers (NVMe spec; Linux: NVME_FEAT_*) */
#define NVME_FEAT_NUM_QUEUES 0x07

/* Identify CNS values */
#define NVME_ID_CNS_NS              0x00
#define NVME_ID_CNS_CTRL            0x01
#define NVME_ID_CNS_ACTIVE_NS_LIST  0x02

/* I/O command opcodes (NVM Command Set) */
#define NVME_CMD_FLUSH 0x00
#define NVME_CMD_WRITE 0x01
#define NVME_CMD_READ  0x02

struct nvme_command {
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
};

struct nvme_completion {
    uint32_t result;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
};

#endif
