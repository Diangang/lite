#ifndef SCSI_SCSI_H
#define SCSI_SCSI_H

#include <stdint.h>

#define TYPE_DISK          0x00

#define TEST_UNIT_READY    0x00
#define INQUIRY            0x12
#define READ_CAPACITY      0x25
#define READ_10            0x28
#define WRITE_10           0x2A
#define REPORT_LUNS        0xA0

#define SCSI_DATA_NONE     0
#define SCSI_DATA_WRITE    1
#define SCSI_DATA_READ     2

#endif
