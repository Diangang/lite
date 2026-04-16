#ifndef LINUX_DMA_DIRECTION_H
#define LINUX_DMA_DIRECTION_H

/*
 * Minimal DMA direction enum (Linux: include/linux/dma-direction.h).
 * Used by scsi_cmnd::sc_data_direction.
 */
enum dma_data_direction {
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE = 1,
    DMA_FROM_DEVICE = 2,
    DMA_NONE = 3,
};

#endif
