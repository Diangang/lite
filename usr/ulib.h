#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>

#define SYS_WRITE 0
#define SYS_YIELD 1
#define SYS_SLEEP 2
#define SYS_EXIT 3
#define SYS_READ 4
#define SYS_GETPID 5
#define SYS_OPEN 6
#define SYS_CLOSE 8
#define SYS_CHDIR 10
#define SYS_GETCWD 11
#define SYS_UNLINK 12
#define SYS_MKDIR 13
#define SYS_EXECVE 14
#define SYS_WAITPID 15
#define SYS_MMAP 18
#define SYS_MUNMAP 19
#define SYS_FORK 20

#define O_CREAT (1<<6)
#define O_TRUNC (1<<9)

int syscall0(int sys_num);
int syscall1(int sys_num, int arg1);
int syscall2(int sys_num, int arg1, int arg2);
int syscall3(int sys_num, int arg1, int arg2, int arg3);

int write(int fd, const void *buf, int count);
int read(int fd, void *buf, int count);
void exit(int status);
int fork(void);
int waitpid(int pid, int *status, int options);
int execve(const char *path);
int open(const char *path, int flags);
int close(int fd);
int unlink(const char *pathname);
void *mmap(void *addr, int length, int prot, int flags, int fd, int offset);
int munmap(void *addr, int length);

void print(const char *str);
void print_int(int val);

#endif
