#ifndef _LINUX_SYSCALLS_H
#define _LINUX_SYSCALLS_H

#include <stdint.h>

int sys_write(int fd, const void *buf, uint32_t len, int from_user);
int sys_read(int fd, void *buf, uint32_t len, int from_user);
int sys_open(const char *pathname, uint32_t flags, int from_user);
int sys_close(int fd);
int sys_chdir(const char *path, int from_user);
int sys_getcwd(char *buf, uint32_t cap, int from_user);
int sys_unlink(const char *pathname, int from_user);
int sys_mkdir(const char *pathname, int from_user);
int sys_getdents(int fd, void *dirp, uint32_t count, int from_user);
int sys_ioctl(int fd, uint32_t request, uint32_t arg);
int sys_waitpid_uapi(uint32_t pid, void *status, uint32_t status_len, int from_user);
int sys_chmod(const char *pathname, uint32_t mode, int from_user);

#endif
