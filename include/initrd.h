#ifndef INITRD_H
#define INITRD_H

#include "fs.h"
#include "multiboot.h"

/*
 * Initial Ramdisk (InitRD) Header
 * We will use a very simple format:
 * [Number of files (4 bytes)]
 * [File 1 Header]
 * [File 2 Header]
 * ...
 * [File 1 Data]
 * [File 2 Data]
 * ...
 */

struct initrd_file_header {
    uint32_t magic; /* Magic number to verify initrd integrity */
    char name[64];
    uint32_t offset;
    uint32_t length;
};

struct vfs_inode *init_initrd(struct multiboot_info* mbi);

#endif