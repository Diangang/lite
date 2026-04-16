#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>
#include "asm/unistd.h"

#define O_CREAT (1<<6)
#define O_TRUNC (1<<9)

int syscall0(int sys_num);
int syscall1(int sys_num, int arg1);
int syscall2(int sys_num, int arg1, int arg2);
int syscall3(int sys_num, int arg1, int arg2, int arg3);

void yield(void);
void sleep(int ticks);

int write(int fd, const void *buf, int count);
int read(int fd, void *buf, int count);
void exit(int status);
int fork(void);
int waitpid(int pid, int *status, int options);
int execve(const char *path);
int open(const char *path, int flags);
int close(int fd);
int unlink(const char *pathname);
int mkdir(const char *pathname);
int rmdir(const char *pathname);
void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
int munmap(void *addr, int length);
int kill(int pid, int sig);

void print(const char *str);
void print_int(int val);

void *memmove(void *dest, const void *src, int n);

#endif
