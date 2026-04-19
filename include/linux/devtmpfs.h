#ifndef LINUX_DEVTMPFS_H
#define LINUX_DEVTMPFS_H

struct inode;

struct inode *devtmpfs_get_console(void);
struct inode *devtmpfs_get_tty(void);

#endif
