#include "ulib.h"

int syscall0(int sys_num) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num));
    return ret;
}

int syscall1(int sys_num, int arg1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(arg1));
    return ret;
}

int syscall2(int sys_num, int arg1, int arg2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(arg1), "c"(arg2));
    return ret;
}

int syscall3(int sys_num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

int syscall6(int sys_num, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6) {
    int ret;
    // We only have limited registers, for mmap we might need to pass arguments in memory or registers
    // In our kernel syscall dispatcher, ebx, ecx, edx, esi, edi, ebp are used.
    __asm__ volatile(
        "push %%ebp\n\t"
        "mov %6, %%ebp\n\t"
        "int $0x80\n\t"
        "pop %%ebp\n\t"
        : "=a"(ret)
        : "a"(sys_num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5), "m"(arg6)
    );
    return ret;
}

int write(int fd, const void *buf, int count) {
    return syscall3(SYS_WRITE, fd, (int)buf, count);
}

int read(int fd, void *buf, int count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

void exit(int status) {
    syscall1(SYS_EXIT, status);
    while(1);
}

int fork(void) {
    return syscall0(SYS_FORK);
}

int waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (int)status, options);
}

int execve(const char *path) {
    return syscall1(SYS_EXECVE, (int)path);
}

int open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

int unlink(const char *pathname) {
    return syscall1(SYS_UNLINK, (int)pathname);
}

void *mmap(void *addr, int length, int prot, int flags, int fd, int offset) {
    // Note: Our mmap only took addr, length, prot currently in assembly. Let's see...
    // In mmaptest.s: ebx=addr (0), ecx=length (0x2000), edx=prot (3)
    // Actually, our kernel might just take ebx, ecx, edx.
    // We'll pass 6 args just in case, or match what the kernel expects.
    return (void *)syscall6(SYS_MMAP, (int)addr, length, prot, flags, fd, offset);
}

int munmap(void *addr, int length) {
    return syscall2(SYS_MUNMAP, (int)addr, length);
}

static int strlen(const char *s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

void print(const char *str) {
    write(1, str, strlen(str));
}

void print_int(int val) {
    char buf[16];
    int i = 0;
    int is_neg = 0;
    if (val < 0) {
        is_neg = 1;
        val = -val;
    }
    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = (val % 10) + '0';
            val /= 10;
        }
    }
    if (is_neg) buf[i++] = '-';

    // reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
    buf[i] = 0;
    print(buf);
}
