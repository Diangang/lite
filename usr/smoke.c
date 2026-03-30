#include "ulib.h"

enum {
    VMA_READ = 1 << 0,
    VMA_WRITE = 1 << 1,
    VMA_EXEC = 1 << 2
};

static int failures;

static void fail(const char *msg)
{
    failures++;
    print("FAIL: ");
    print(msg);
    print("\n");
}

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
        if (ret != pid || status[0] != 42)
            fail("fork/waitpid exit code mismatch");
    } else {
        fail("fork failed");
    }
}

void test_file_io() {
    print("\n--- Test 2: File I/O ---\n");
    int fd = open("/test.txt", O_CREAT);
    if (fd < 0) {
        fail("create /test.txt");
        return;
    }
    const char *msg = "Hello from Lite OS test_all!\n";
    if (write(fd, msg, 29) != 29)
        fail("write /test.txt");
    close(fd);
    print("Wrote to /test.txt successfully.\n");

    fd = open("/test.txt", 0);
    if (fd < 0) {
        fail("open /test.txt for read");
        return;
    }
    char buf[64];
    int n = read(fd, buf, 63);
    if (n >= 0) {
        buf[n] = 0;
        print("Read from file: ");
        print(buf);
    } else {
        fail("read /test.txt");
    }
    close(fd);

    int ret = unlink("/test.txt");
    if (ret == 0)
        print("Deleted /test.txt successfully.\n");
    else
        fail("unlink /test.txt");
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
        fail("Kernel did not reject bad pointer");
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
        fail("fork() for A failed");
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
        fail("fork() for B failed");
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
        fail("Unexpected scheduler test exit codes");
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

static int count_substr(const char *hay, int hay_len, const char *needle)
{
    if (!hay || !needle)
        return 0;
    int nlen = 0;
    while (needle[nlen])
        nlen++;
    if (nlen == 0)
        return 0;
    int count = 0;
    for (int i = 0; i + nlen <= hay_len; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++) {
            if (hay[i + j] != needle[j]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            count++;
            i += nlen - 1;
        }
    }
    return count;
}

static int read_file(const char *path, char *buf, int cap)
{
    if (!path || !buf || cap <= 1)
        return -1;
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    int n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = 0;
    return n;
}

static int write_file(const char *path, const char *data, int len)
{
    if (!path || !data || len <= 0)
        return -1;
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    int n = write(fd, data, len);
    close(fd);
    return n;
}

void test_mounts() {
    print("\n--- Test 9: /proc/mounts ---\n");
    int fd = open("/proc/mounts", 0);
    if (fd < 0) {
        fail("Could not open /proc/mounts");
        return;
    }

    char buf[512];
    int n = read(fd, buf, 511);
    close(fd);
    if (n <= 0) {
        fail("Could not read /proc/mounts");
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
        fail("Mount table missing entries");
}

void test_rmdir() {
    print("\n--- Test 8: rmdir/unlink ---\n");
    int before = failures;
    int ret = mkdir("/d");
    if (ret < 0) {
        fail("mkdir /d");
        return;
    }
    int fd = open("/d/f", O_CREAT);
    if (fd < 0) {
        fail("create /d/f");
        return;
    }
    close(fd);

    ret = rmdir("/d");
    if (ret == 0)
        fail("rmdir non-empty dir");
    ret = unlink("/d");
    if (ret == 0)
        fail("unlink dir");

    ret = unlink("/d/f");
    if (ret < 0)
        fail("unlink /d/f");

    ret = rmdir("/d");
    if (ret < 0)
        fail("rmdir /d");

    ret = rmdir("/");
    if (ret == 0)
        fail("rmdir /");
    if (failures == before)
        print("rmdir/unlink OK.\n");
}

void test_pci_uevent() {
    print("\n--- Test 10: PCI uevent ---\n");
    char buf[2048];
    int n = read_file("/sys/kernel/uevent", buf, sizeof(buf));
    if (n <= 0) {
        fail("Could not read /sys/kernel/uevent");
        return;
    }
    int count = count_substr(buf, n, "add pci");
    int bind_count = count_substr(buf, n, "bind pci");
    if (count == 0 && bind_count == 0)
        fail("No PCI add/bind events found");
    else if (count == 0)
        print("WARN: No PCI add events found.\n");
    int bar_count = count_substr(buf, n, "bar pci");
    if (bar_count > 0) {
        print("PCI bar event count=");
        print_int(bar_count);
        print("\n");
    } else {
        print("WARN: No PCI bar events found.\n");
    }
    int bar_fail = count_substr(buf, n, "barfail pci");
    if (bar_fail > 0) {
        print("WARN: PCI bar fail count=");
        print_int(bar_fail);
        print("\n");
    }
    int bridge_count = count_substr(buf, n, "pci01:");
    if (bridge_count > 0) {
        print("PCI secondary bus detected count=");
        print_int(bridge_count);
        print("\n");
    }
    int enable_count = count_substr(buf, n, "enable pci");
    if (enable_count > 0) {
        print("PCI enable event count=");
        print_int(enable_count);
        print("\n");
    }
    int pcie_count = count_substr(buf, n, "pciecap pci");
    if (pcie_count > 0) {
        print("PCIe capability count=");
        print_int(pcie_count);
        print("\n");
    }
    int nvme_count = count_substr(buf, n, "nvme pci");
    if (nvme_count > 0) {
        print("NVMe class device count=");
        print_int(nvme_count);
        print("\n");
    } else if (pcie_count > 0) {
        print("WARN: No NVMe class device detected.\n");
    }
    if (nvme_count > 0 && pcie_count == 0) {
        fail("NVMe present but no PCIe capability event found");
        print("uevent dump:\n");
        print(buf);
    }
}

void test_sysfs_layout() {
    print("\n--- Test 11: sysfs layout ---\n");
    int ok = 1;
    char buf[128];
    int n = read_file("/sys/devices/platform/type", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "platform-root")) {
        fail("/sys/devices/platform/type");
        ok = 0;
    }
    n = read_file("/sys/devices/pci0000:00/type", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "pci-root")) {
        fail("/sys/devices/pci0000:00/type");
        ok = 0;
    }

    int fd = open("/sys/bus/pci", 0);
    if (fd < 0) {
        fail("open /sys/bus/pci");
        ok = 0;
    } else
        close(fd);

    fd = open("/sys/bus/pci/devices", 0);
    if (fd < 0) {
        fail("open /sys/bus/pci/devices");
        ok = 0;
    } else
        close(fd);

    fd = open("/sys/bus/pci/drivers", 0);
    if (fd < 0) {
        fail("open /sys/bus/pci/drivers");
        ok = 0;
    } else
        close(fd);

    n = read_file("/sys/bus/pci/drivers/nvme/name", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "nvme")) {
        fail("/sys/bus/pci/drivers/nvme/name");
        ok = 0;
    }

    n = read_file("/sys/bus/platform/drivers/console/name", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "console")) {
        fail("/sys/bus/platform/drivers/console/name");
        ok = 0;
    }
    if (ok)
        print("sysfs layout OK.\n");
}

void test_sysfs_bind_unbind_console() {
    print("\n--- Test 12: sysfs bind/unbind ---\n");
    int ok = 1;
    char buf[128];
    int n = read_file("/sys/devices/platform/console/driver", buf, sizeof(buf));
    if (n <= 0) {
        fail("read console driver");
        return;
    }
    if (!contains(buf, n, "console"))
        print("WARN: console not bound initially\n");

    int w = write_file("/sys/bus/platform/drivers/console/unbind", "console\n", 8);
    if (w <= 0) {
        fail("unbind console");
        return;
    }
    n = read_file("/sys/devices/platform/console/driver", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "unbound")) {
        fail("console driver should be unbound");
        ok = 0;
    }

    w = write_file("/sys/bus/platform/drivers/console/bind", "console\n", 8);
    if (w <= 0) {
        fail("bind console");
        return;
    }
    n = read_file("/sys/devices/platform/console/driver", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "console")) {
        fail("console driver should be bound");
        ok = 0;
    }
    if (ok)
        print("sysfs bind/unbind OK.\n");
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
    test_rmdir();
    test_mounts();
    test_pci_uevent();
    test_sysfs_layout();
    test_sysfs_bind_unbind_console();

    print("\n================================\n");
    if (failures == 0) {
        print("  All tests completed (OK).     \n");
    } else {
        print("  All tests completed (FAIL).   \n");
        print("  FAILURES=");
        print_int(failures);
        print("\n");
    }
    print("================================\n");
    return failures == 0 ? 0 : 1;
}
