#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include "fs.h"

typedef struct vfs_file vfs_file_t;

enum {
    O_CREAT = 1 << 6,
    O_TRUNC = 1 << 9
};

typedef struct file {
    fs_node_t *node;
    uint32_t flags;
    vfs_file_t *vf;
} file_t;

file_t *file_open_node(fs_node_t *node, uint32_t flags);
file_t *file_open_path(const char *path, uint32_t flags);
uint32_t file_read(file_t *f, uint8_t *buf, uint32_t len);
uint32_t file_write(file_t *f, const uint8_t *buf, uint32_t len);
int file_ioctl(file_t *f, uint32_t request, uint32_t arg);
void file_close(file_t *f);

#endif
