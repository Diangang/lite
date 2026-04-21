#ifndef ASM_UNISTD_H
#define ASM_UNISTD_H

enum {
    SYS_WRITE = 0,
    SYS_YIELD = 1,
    SYS_SLEEP = 2,
    SYS_EXIT = 3,
    SYS_READ = 4,
    SYS_GETPID = 5,
    SYS_OPEN = 6,
    SYS_CLOSE = 8,
    SYS_BRK = 9,
    SYS_CHDIR = 10,
    SYS_GETCWD = 11,
    SYS_UNLINK = 12,
    SYS_MKDIR = 13,
    SYS_EXECVE = 14,
    SYS_WAITPID = 15,
    SYS_IOCTL = 16,
    SYS_KILL = 17,
    SYS_MMAP = 18,
    SYS_MUNMAP = 19,
    SYS_MPROTECT = 20,
    SYS_MREMAP = 21,
    SYS_FORK = 22,
    SYS_GETDENTS = 23,
    SYS_UMASK = 24,
    SYS_CHMOD = 25,
    SYS_GETUID = 26,
    SYS_GETGID = 27,
    SYS_RMDIR = 28
};

/* Linux mapping: NR_syscalls is used for syscall table sizing/bounds. */
enum { NR_syscalls = 32 };

#endif
