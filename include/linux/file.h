#ifndef LINUX_FILE_H
#define LINUX_FILE_H

#include <stdint.h>
#include "linux/fs.h"

enum {
    O_CREAT = 1 << 6,
    O_TRUNC = 1 << 9
};

struct file {
    struct dentry *dentry;
    uint32_t pos;
    uint32_t flags;
    uint32_t refcount;
};

struct file *file_open_node(struct inode *node, uint32_t flags);
struct file *file_open_path(const char *path, uint32_t flags);
uint32_t file_read(struct file *f, uint8_t *buf, uint32_t len);
uint32_t file_write(struct file *f, const uint8_t *buf, uint32_t len);
int file_ioctl(struct file *f, uint32_t request, uint32_t arg);
struct file *file_dup(struct file *f);
void file_close(struct file *f);

#endif
