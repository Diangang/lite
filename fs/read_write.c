#include "linux/fs.h"
#include "linux/file.h"
#include "linux/fdtable.h"
#include "asm/pgtable.h"
#include "linux/libc.h"
#include "linux/uaccess.h"

uint32_t read_fs(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->read != NULL)
        return node->f_ops->read(node, offset, size, buffer);
    return 0;
}

uint32_t write_fs(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (node->f_ops && node->f_ops->write != NULL)
        return node->f_ops->write(node, offset, size, buffer);
    return 0;
}

int sys_write(int fd, const void *buf, uint32_t len, int from_user)
{
    if (len > 4096)
        return -1;
    if (from_user) {
        if (!access_ok(get_pgd_current(), (void*)buf, len, 0))
            return -1;
    }

    struct file *f = fget(fd);
    if (!f)
        return -1;

    char tmp[256];
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > sizeof(tmp))
            chunk = sizeof(tmp);

        if (from_user) {
            if (copy_from_user(tmp, (void*)((uint32_t)buf + off), chunk) != 0)
                return -1;
        } else {
            memcpy(tmp, (void*)((uint32_t)buf + off), chunk);
        }

        uint32_t n = file_write(f, (uint8_t*)tmp, chunk);
        if (n == 0)
            break;
        off += n;
        if (n < chunk)
            break;
    }
    return (int)off;
}

int sys_read(int fd, void *buf, uint32_t len, int from_user)
{
    if (len > 4096)
        return -1;
    if (from_user) {
        if (!access_ok(get_pgd_current(), buf, len, 1))
            return -1;
    }

    struct file *f = fget(fd);
    if (!f)
        return -1;

    char tmp[256];
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > sizeof(tmp))
            chunk = sizeof(tmp);

        uint32_t n = file_read(f, (uint8_t*)tmp, chunk);
        if (n == 0)
            break;

        if (from_user) {
            if (copy_to_user((void*)((uint32_t)buf + off), tmp, n) != 0)
                return -1;
        } else {
            memcpy((void*)((uint32_t)buf + off), tmp, n);
        }

        off += n;
        if (n < chunk)
            break;
    }
    return (int)off;
}
