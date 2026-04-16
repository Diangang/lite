#ifndef SCSI_SCSI_CMND_H
#define SCSI_SCSI_CMND_H

#include <stdint.h>
#include "linux/dma-direction.h"

/*
 * Minimal scsi_cmnd (Linux: include/scsi/scsi_cmnd.h).
 * Lite currently executes commands synchronously, but keeps the Linux
 * completion shape via scsi_done.
 */

#define MAX_COMMAND_SIZE 16
#define SCSI_SENSE_BUFFERSIZE 96

struct scsi_device;

struct scsi_cmnd {
    struct scsi_device *device;
    unsigned char cmnd[MAX_COMMAND_SIZE];
    unsigned short cmd_len;
    enum dma_data_direction sc_data_direction;

    void *request_buffer;
    unsigned int request_bufflen;

    unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];
    unsigned int sense_len;

    int result; /* low byte: SCSI status */
    void (*scsi_done)(struct scsi_cmnd *sc);
    void *host_scribble;
};

static inline uint8_t scsi_status_byte(int result)
{
    return (uint8_t)(result & 0xFF);
}

#endif
