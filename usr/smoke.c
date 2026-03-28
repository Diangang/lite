#include "ulib.h"

enum {
    VMA_READ = 1 << 0,
    VMA_WRITE = 1 << 1,
    VMA_EXEC = 1 << 2
};

void test_fork() {
    print("\n--- Test 1: Fork & Waitpid ---\n");
    int pid = fork();
    if (pid == 0) {
        print("Child process executing...\n");
        exit(42);
    } else if (pid > 0) {
        print("Parent process waiting for child (PID: ");
        print_int(pid);
        print(")...\n");

        int status[4] = {0};
        int ret = waitpid(pid, status, 16);
        print("Child exited with waitpid ret=");
        print_int(ret);
        print(", exit_code=");
        print_int(status[0]);
        print(" (should be 42)\n");
    } else {
        print("Fork failed!\n");
    }
}

void test_file_io() {
    print("\n--- Test 2: File I/O ---\n");
    int fd = open("/test.txt", O_CREAT);
    if (fd < 0) {
        print("Failed to create file\n");
        return;
    }
    const char *msg = "Hello from Lite OS test_all!\n";
    write(fd, msg, 29);
    close(fd);
    print("Wrote to /test.txt successfully.\n");

    fd = open("/test.txt", 0);
    if (fd < 0) {
        print("Failed to open file for reading\n");
        return;
    }
    char buf[64];
    int n = read(fd, buf, 63);
    if (n >= 0) {
        buf[n] = 0;
        print("Read from file: ");
        print(buf);
    } else {
        print("Read failed!\n");
    }
    close(fd);

    int ret = unlink("/test.txt");
    if (ret == 0)
        print("Deleted /test.txt successfully.\n");
    else
        print("FAIL: Could not delete /test.txt\n");
}

void test_pf() {
    print("\n--- Test 3: Page Fault Isolation ---\n");
    print("Forking a child to crash (write to NULL)...\n");
    int pid = fork();
    if (pid == 0) {
        volatile int *bad_ptr = (int *)0x0;
        *bad_ptr = 0xDEADBEEF; // This should cause a page fault and kill the child
        exit(0);
    } else if (pid > 0) {
        int status[4] = {0};
        waitpid(pid, status, 16);
        print("Child killed by page fault, parent survives!\n");
        print("Child exit reason: ");
        print_int(status[1]); // TASK_EXIT_PAGEFAULT
        print("\n");
    }
}

void test_mmap() {
    print("\n--- Test 4: MMAP ---\n");
    char *addr = (char *)mmap(0, 0x2000, 3, 0, 0, 0); // addr=0, length=0x2000, prot=3
    if ((int)addr == -1) {
        print("mmap failed!\n");
        return;
    }
    print("mmap succeeded at addr: ");
    print_int((int)addr);
    print("\n");

    addr[0] = 'M';
    addr[1] = 'M';
    addr[2] = 'A';
    addr[3] = 'P';
    addr[4] = '\n';
    addr[5] = '\0';

    print("Read from mmap memory: ");
    print(addr);

    int ret = munmap(addr, 0x2000);
    if (ret == 0)
        print("munmap succeeded.\n"); else {
        print("munmap failed!\n");
    }
}

void test_mprotect_mremap() {
    print("\n--- Test 5: MPROTECT & MREMAP ---\n");
    void *base = mmap(0, 8192, VMA_READ | VMA_WRITE, 0, -1, 0);
    if (!base) {
        print("mmap failed!\n");
        return;
    }

    char *p = (char*)base;
    p[0] = 'O';
    p[1] = 'K';

    int prot_res = syscall3(SYS_MPROTECT, (int)base, 8192, VMA_READ);
    if (prot_res < 0) {
        print("mprotect failed!\n");
        return;
    }

    int remap = syscall3(SYS_MREMAP, (int)base, 8192, 4096);
    if (remap == 0) {
        print("mremap failed!\n");
        return;
    }
    print("mprotect+mremap OK\n");
}

void test_bad_ptr() {
    print("\n--- Test 6: Bad Pointer in Syscall ---\n");
    print("Passing invalid pointer (0x08000000) to write()...\n");
    int ret = write(1, (void*)0x08000000, 4);
    if (ret == -1)
        print("Kernel correctly rejected bad pointer (returned -1).\n"); else {
        print("FAIL: Kernel did not reject bad pointer! Returned: ");
        print_int(ret);
        print("\n");
    }
}

void test_sched() {
    print("\n--- Test 7: User Scheduler (fork + sleep + yield) ---\n");
    int pid_a = fork();
    if (pid_a == 0) {
        for (int i = 0; i < 20; i++) {
            write(1, "A", 1);
            sleep(1);
            yield();
        }
        write(1, "\n", 1);
        exit(11);
    }
    if (pid_a < 0) {
        print("FAIL: fork() for A failed\n");
        return;
    }

    int pid_b = fork();
    if (pid_b == 0) {
        for (int i = 0; i < 20; i++) {
            write(1, "B", 1);
            sleep(1);
            yield();
        }
        write(1, "\n", 1);
        exit(22);
    }
    if (pid_b < 0) {
        print("FAIL: fork() for B failed\n");
        return;
    }

    int st_a[4] = {0};
    int st_b[4] = {0};
    waitpid(pid_a, st_a, 16);
    waitpid(pid_b, st_b, 16);
    print("A exit_code=");
    print_int(st_a[0]);
    print(", B exit_code=");
    print_int(st_b[0]);
    print("\n");
    if (st_a[0] == 11 && st_b[0] == 22)
        print("Scheduler test OK.\n");
    else
        print("FAIL: Unexpected scheduler test exit codes.\n");
}

static int contains(const char *hay, int hay_len, const char *needle)
{
    if (!hay || !needle)
        return 0;
    int nlen = 0;
    while (needle[nlen])
        nlen++;
    if (nlen == 0)
        return 1;
    for (int i = 0; i + nlen <= hay_len; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++) {
            if (hay[i + j] != needle[j]) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return 1;
    }
    return 0;
}

void test_mounts() {
    print("\n--- Test 8: /proc/mounts ---\n");
    int fd = open("/proc/mounts", 0);
    if (fd < 0) {
        print("FAIL: Could not open /proc/mounts\n");
        return;
    }

    char buf[512];
    int n = read(fd, buf, 511);
    close(fd);
    if (n <= 0) {
        print("FAIL: Could not read /proc/mounts\n");
        return;
    }
    buf[n] = 0;
    print("mounts:\n");
    print(buf);

    int ok = 1;
    if (!contains(buf, n, "ramfs /"))
        ok = 0;
    if (!contains(buf, n, "proc /proc"))
        ok = 0;
    if (!contains(buf, n, "devtmpfs /dev"))
        ok = 0;
    if (!contains(buf, n, "sysfs /sys"))
        ok = 0;

    if (ok)
        print("Mount table looks OK.\n");
    else
        print("FAIL: Mount table missing entries.\n");
}

int main() {
    print("================================\n");
    print("  Lite OS Automated Test Suite  \n");
    print("================================\n");

    test_fork();
    test_file_io();
    test_pf();
    test_mmap();
    test_mprotect_mremap();
    test_bad_ptr();
    test_sched();
    test_mounts();

    print("\n================================\n");
    print("  All tests completed!          \n");
    print("================================\n");
    return 0;
}
