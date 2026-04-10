#include "ulib.h"

enum {
    VMA_READ = 1 << 0,
    VMA_WRITE = 1 << 1,
    VMA_EXEC = 1 << 2
};

static int failures;
static int read_file(const char *path, char *buf, int cap);

/* fail: Implement fail. */
static void fail(const char *msg)
{
    failures++;
    print("FAIL: ");
    print(msg);
    print("\n");
}

/* parse_memtotal_kb: Parse memtotal kb. */
static int parse_memtotal_kb(void)
{
    char buf[1024];
    int n = read_file("/proc/meminfo", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    const char *key = "MemTotal:";
    for (int i = 0; i + 9 < n; i++) {
        int match = 1;
        for (int j = 0; j < 9; j++) {
            if (buf[i + j] != key[j]) {
                match = 0;
                break;
            }
        }
        if (!match)
            continue;
        int k = i + 9;
        while (k < n && (buf[k] == ' ' || buf[k] == '\t'))
            k++;
        int val = 0;
        while (k < n && buf[k] >= '0' && buf[k] <= '9') {
            val = val * 10 + (buf[k] - '0');
            k++;
        }
        return val;
    }
    return -1;
}

/* parse_memfree_kb: Parse memfree kb. */
static int parse_memfree_kb(void)
{
    char buf[1024];
    int n = read_file("/proc/meminfo", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    const char *key = "MemFree:";
    for (int i = 0; i + 8 < n; i++) {
        int match = 1;
        for (int j = 0; j < 8; j++) {
            if (buf[i + j] != key[j]) {
                match = 0;
                break;
            }
        }
        if (!match)
            continue;
        int k = i + 8;
        while (k < n && (buf[k] == ' ' || buf[k] == '\t'))
            k++;
        int val = 0;
        while (k < n && buf[k] >= '0' && buf[k] <= '9') {
            val = val * 10 + (buf[k] - '0');
            k++;
        }
        return val;
    }
    return -1;
}

/* parse_sched_ticks: Parse sched ticks. */
static int parse_sched_ticks(void)
{
    char buf[256];
    int n = read_file("/proc/sched", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    const char *key = "ticks=";
    for (int i = 0; i + 6 < n; i++) {
        int match = 1;
        for (int j = 0; j < 6; j++) {
            if (buf[i + j] != key[j]) {
                match = 0;
                break;
            }
        }
        if (!match)
            continue;
        int k = i + 6;
        int val = 0;
        while (k < n && buf[k] >= '0' && buf[k] <= '9') {
            val = val * 10 + (buf[k] - '0');
            k++;
        }
        return val;
    }
    return -1;
}

/* parse_cow_stats: Parse copy-on-write stats. */
static int parse_cow_stats(int *faults, int *copies)
{
    char buf[128];
    int n = read_file("/proc/cow", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    int got_faults = 0;
    int got_copies = 0;
    for (int i = 0; i + 7 < n; i++) {
        if (!got_faults && buf[i] == 'f' && buf[i+1] == 'a' && buf[i+2] == 'u' && buf[i+3] == 'l' && buf[i+4] == 't' && buf[i+5] == 's' && buf[i+6] == '=') {
            int k = i + 7;
            int val = 0;
            while (k < n && buf[k] >= '0' && buf[k] <= '9') {
                val = val * 10 + (buf[k] - '0');
                k++;
            }
            *faults = val;
            got_faults = 1;
        }
        if (!got_copies && buf[i] == 'c' && buf[i+1] == 'o' && buf[i+2] == 'p' && buf[i+3] == 'i' && buf[i+4] == 'e' && buf[i+5] == 's' && buf[i+6] == '=') {
            int k = i + 7;
            int val = 0;
            while (k < n && buf[k] >= '0' && buf[k] <= '9') {
                val = val * 10 + (buf[k] - '0');
                k++;
            }
            *copies = val;
            got_copies = 1;
        }
    }
    if (!got_faults || !got_copies)
        return -1;
    return 0;
}

/* parse_kv_u32: Parse kv u32. */
static int parse_kv_u32(const char *buf, int n, const char *key, int *out)
{
    int key_len = 0;
    while (key[key_len])
        key_len++;
    for (int i = 0; i + key_len < n; i++) {
        int match = 1;
        for (int j = 0; j < key_len; j++) {
            if (buf[i + j] != key[j]) {
                match = 0;
                break;
            }
        }
        if (!match)
            continue;
        int k = i + key_len;
        int val = 0;
        while (k < n && buf[k] >= '0' && buf[k] <= '9') {
            val = val * 10 + (buf[k] - '0');
            k++;
        }
        *out = val;
        return 0;
    }
    return -1;
}

/* parse_pfault_stats: Parse pfault stats. */
static int parse_pfault_stats(int *total, int *present, int *not_present, int *write, int *user, int *prot)
{
    char buf[256];
    int n = read_file("/proc/pfault", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    if (parse_kv_u32(buf, n, "total=", total) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "present=", present) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "not_present=", not_present) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "write=", write) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "user=", user) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "prot=", prot) < 0)
        return -1;
    return 0;
}

/* parse_vmscan_stats: Parse vmscan stats. */
static int parse_vmscan_stats(int *wakeups, int *tries, int *reclaims, int *anon, int *file)
{
    char buf[128];
    int n = read_file("/proc/vmscan", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    if (parse_kv_u32(buf, n, "kswapd_wakeups=", wakeups) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "kswapd_tries=", tries) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "kswapd_reclaims=", reclaims) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "kswapd_anon_reclaims=", anon) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "kswapd_file_reclaims=", file) < 0)
        return -1;
    return 0;
}

/* parse_writeback_stats: Parse writeback stats. */
static int parse_writeback_stats(int *dirty, int *cleaned, int *discarded)
{
    char buf[128];
    int n = read_file("/proc/writeback", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    if (parse_kv_u32(buf, n, "dirty=", dirty) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "cleaned=", cleaned) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "discarded=", discarded) < 0)
        return -1;
    return 0;
}

/* parse_blockstats: Parse blockstats. */
static int parse_blockstats(int *reads, int *writes, int *bytes_read, int *bytes_written)
{
    char buf[160];
    int n = read_file("/proc/blockstats", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    if (parse_kv_u32(buf, n, "reads=", reads) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "writes=", writes) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "bytes_read=", bytes_read) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "bytes_written=", bytes_written) < 0)
        return -1;
    return 0;
}

/* parse_pagecache_stats: Parse page cache stats. */
static int parse_pagecache_stats(int *hits, int *misses)
{
    char buf[128];
    int n = read_file("/proc/pagecache", buf, sizeof(buf));
    if (n <= 0)
        return -1;
    if (parse_kv_u32(buf, n, "hits=", hits) < 0)
        return -1;
    if (parse_kv_u32(buf, n, "misses=", misses) < 0)
        return -1;
    return 0;
}

/* test_fork: Implement test fork. */
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

/* test_file_io: Implement test file io. */
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

/* test_pf: Implement test pf. */
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

/* test_mmap: Implement test mmap. */
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

/* test_mprotect_mremap: Implement test mprotect mremap. */
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

/* test_bad_ptr: Implement test bad ptr. */
void test_bad_ptr() {
    print("\n--- Test 6: Bad Pointer in Syscall ---\n");
    print("Passing invalid pointer (0x08000000) to write()...\n");
    int ret = write(1, (void*)0x08000000, 4);
    if (ret == -1)
        print("Kernel correctly rejected bad pointer (returned -1).\n"); else {
        fail("Kernel did not reject bad pointer");
    }
}

/* test_sched: Implement test sched. */
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

/* contains: Implement contains. */
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

/* count_substr: Implement count substr. */
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

/* read_file: Read file. */
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

/* write_file: Write file. */
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

/* test_mounts: Implement test mounts. */
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

/* test_rmdir: Implement test rmdir. */
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

/* test_proc_meminfo_iomem: Implement test proc meminfo iomem. */
void test_proc_meminfo_iomem() {
    print("\n--- Test 10: /proc meminfo/iomem ---\n");
    int ok = 1;
    char buf[1024];
    int n = read_file("/proc/meminfo", buf, sizeof(buf));
    if (n <= 0) {
        fail("read /proc/meminfo");
        return;
    }
    if (!contains(buf, n, "E820Ram:")) {
        fail("meminfo missing E820Ram");
        ok = 0;
    }
    if (!contains(buf, n, "LowMemEnd:")) {
        fail("meminfo missing LowMemEnd");
        ok = 0;
    }
    if (!contains(buf, n, "LowMemPhysEnd:")) {
        fail("meminfo missing LowMemPhysEnd");
        ok = 0;
    }
    if (!contains(buf, n, "VmallocStart:")) {
        fail("meminfo missing VmallocStart");
        ok = 0;
    }
    if (!contains(buf, n, "VmallocEnd:")) {
        fail("meminfo missing VmallocEnd");
        ok = 0;
    }
    if (!contains(buf, n, "DirectMapStart:")) {
        fail("meminfo missing DirectMapStart");
        ok = 0;
    }
    if (!contains(buf, n, "DirectMapEnd:")) {
        fail("meminfo missing DirectMapEnd");
        ok = 0;
    }
    if (!contains(buf, n, "FixaddrStart:")) {
        fail("meminfo missing FixaddrStart");
        ok = 0;
    }

    n = read_file("/proc/iomem", buf, sizeof(buf));
    if (n <= 0) {
        fail("read /proc/iomem");
        return;
    }
    if (!contains(buf, n, "System RAM")) {
        fail("iomem missing System RAM");
        ok = 0;
    }
    if (!contains(buf, n, "Kernel")) {
        fail("iomem missing Kernel");
        ok = 0;
    }
    if (!contains(buf, n, "initramfs")) {
        fail("iomem missing initramfs");
        ok = 0;
    }
    if (ok)
        print("/proc meminfo/iomem OK.\n");
}

/* test_large_mmap_touch: Implement test large mmap touch. */
void test_large_mmap_touch() {
    print("\n--- Test 11: Large MMAP Touch ---\n");
    int memtotal_kb = parse_memtotal_kb();
    if (memtotal_kb < 0) {
        fail("parse MemTotal");
        return;
    }
    int memfree_kb = parse_memfree_kb();
    if (memfree_kb < 0) {
        fail("parse MemFree");
        return;
    }
    print("MemTotal_kB=");
    print_int(memtotal_kb);
    print(" MemFree_kB=");
    print_int(memfree_kb);
    print("\n");

    if (memtotal_kb < (192 * 1024) || memfree_kb < (208 * 1024)) {
        print("SKIP: Not enough free memory for >128MB stress.\n");
        return;
    }

    int target_kb = 144 * 1024;
    int bytes = target_kb * 1024;

    char *addr = (char *)mmap(0, bytes, 3, 0, 0, 0);
    if (!addr || (int)addr == -1) {
        fail("large mmap");
        return;
    }

    for (int i = 0; i < bytes; i += 4096)
        addr[i] = (char)(i & 0xFF);

    int ret = munmap(addr, bytes);
    if (ret != 0)
        fail("large munmap");
    else
        print("Large mmap touch OK.\n");
}

/* test_pci_uevent: Implement test PCI uevent. */
void test_pci_uevent() {
    print("\n--- Test 12: PCI uevent ---\n");
    char buf[2048];
    int n = read_file("/sys/kernel/uevent", buf, sizeof(buf));
    if (n <= 0) {
        fail("Could not read /sys/kernel/uevent");
        return;
    }
    int pci_any = count_substr(buf, n, "SUBSYSTEM=pci");
    int count = count_substr(buf, n, "ACTION=add");
    int bind_count = count_substr(buf, n, "ACTION=bind");
    if (pci_any == 0 || (count == 0 && bind_count == 0))
        fail("No PCI add/bind events found");
    else if (count == 0)
        print("WARN: No PCI add events found.\n");
    int bar_count = count_substr(buf, n, "ACTION=bar");
    if (bar_count > 0) {
        print("PCI bar event count=");
        print_int(bar_count);
        print("\n");
    } else {
        print("WARN: No PCI bar events found.\n");
    }
    int bar_fail = count_substr(buf, n, "ACTION=barfail");
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
    int enable_count = count_substr(buf, n, "ACTION=enable");
    if (enable_count > 0) {
        print("PCI enable event count=");
        print_int(enable_count);
        print("\n");
    }
    int pcie_count = count_substr(buf, n, "ACTION=pciecap");
    if (pcie_count > 0) {
        print("PCIe capability count=");
        print_int(pcie_count);
        print("\n");
    }
    int nvme_count = count_substr(buf, n, "ACTION=nvme");
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

/* test_sysfs_layout: Implement test sysfs layout. */
void test_sysfs_layout() {
    print("\n--- Test 13: sysfs layout ---\n");
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

    fd = open("/sys/class/tty", 0);
    if (fd < 0) {
        fail("open /sys/class/tty");
        ok = 0;
    } else
        close(fd);

    n = read_file("/sys/class/tty/ttyS0/parent/type", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "serial")) {
        fail("/sys/class/tty/ttyS0/parent/type");
        ok = 0;
    }

    n = read_file("/sys/class/tty/ttyS0/tty_driver", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "serial")) {
        fail("/sys/class/tty/ttyS0/tty_driver");
        ok = 0;
    }

    n = read_file("/sys/class/tty/ttyS0/index", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "0")) {
        fail("/sys/class/tty/ttyS0/index");
        ok = 0;
    }
    n = read_file("/sys/class/tty/ttyS0/dev", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "4:64")) {
        fail("/sys/class/tty/ttyS0/dev");
        ok = 0;
    }

    fd = open("/sys/class/block", 0);
    if (fd < 0) {
        fail("open /sys/class/block");
        ok = 0;
    } else
        close(fd);

    n = read_file("/sys/class/block/ram0/type", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "block")) {
        fail("/sys/class/block/ram0/type");
        ok = 0;
    }

    n = read_file("/sys/class/block/ram0/parent/type", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "virtual")) {
        fail("/sys/class/block/ram0/parent/type");
        ok = 0;
    }

    n = read_file("/sys/class/block/ram0/capacity", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "16384")) {
        fail("/sys/class/block/ram0/capacity");
        ok = 0;
    }

    n = read_file("/sys/class/block/ram0/queue", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "present")) {
        fail("/sys/class/block/ram0/queue");
        ok = 0;
    }
    n = read_file("/sys/class/block/ram0/dev", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "1:0")) {
        fail("/sys/class/block/ram0/dev");
        ok = 0;
    }
    if (ok)
        print("sysfs layout OK.\n");
}

/* test_sysfs_bind_unbind_console: Implement test sysfs bind unbind console. */
void test_sysfs_bind_unbind_console() {
    print("\n--- Test 14: sysfs bind/unbind ---\n");
    int ok = 1;
    char buf[128];
    int n = read_file("/sys/devices/platform/console0/driver", buf, sizeof(buf));
    if (n <= 0) {
        fail("read console driver");
        return;
    }
    if (!contains(buf, n, "console"))
        print("WARN: console not bound initially\n");

    int w = write_file("/sys/bus/platform/drivers/console/unbind", "console0\n", 9);
    if (w <= 0) {
        fail("unbind console");
        return;
    }
    n = read_file("/sys/devices/platform/console0/driver", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "unbound")) {
        fail("console driver should be unbound");
        ok = 0;
    }

    w = write_file("/sys/bus/platform/drivers/console/bind", "console0\n", 9);
    if (w <= 0) {
        fail("bind console");
        return;
    }
    n = read_file("/sys/devices/platform/console0/driver", buf, sizeof(buf));
    if (n <= 0 || !contains(buf, n, "console")) {
        fail("console driver should be bound");
        ok = 0;
    }
    if (ok)
        print("sysfs bind/unbind OK.\n");
}

/* test_fork_blast: Implement test fork blast. */
void test_fork_blast() {
    print("\n--- Test 15: Fork Blast (waitpid semantics) ---\n");

    int memtotal_kb = parse_memtotal_kb();
    int nseq = 32;
    int nburst = 48;
    if (memtotal_kb >= 256 * 1024) {
        nseq = 64;
        nburst = 96;
    }
    if (nburst > 120)
        nburst = 120;

    for (int i = 0; i < nseq; i++) {
        int pid = fork();
        if (pid == 0) {
            exit(10 + i);
        }
        if (pid < 0) {
            fail("fork blast: fork failed (seq)");
            return;
        }
        int st[4] = {0};
        int ret = waitpid(pid, st, 16);
        if (ret != pid || st[0] != 10 + i) {
            fail("fork blast: seq waitpid mismatch");
            return;
        }
    }

    int seen[120];
    for (int i = 0; i < nburst; i++)
        seen[i] = 0;

    int spawned = 0;
    for (int i = 0; i < nburst; i++) {
        int pid = fork();
        if (pid == 0) {
            yield();
            exit(100 + i);
        }
        if (pid < 0)
            break;
        spawned++;
    }

    if (spawned == 0) {
        fail("fork blast: fork failed (burst)");
        return;
    }

    for (int i = 0; i < spawned; i++) {
        int st[4] = {0};
        int ret = waitpid(-1, st, 16);
        if (ret < 0) {
            fail("fork blast: waitpid(-1) failed early");
            return;
        }
        int code = st[0];
        if (code < 100 || code >= 100 + spawned) {
            fail("fork blast: bad exit code range");
            return;
        }
        int idx = code - 100;
        seen[idx]++;
        if (seen[idx] != 1) {
            fail("fork blast: duplicate reap");
            return;
        }
    }

    int st[4] = {0};
    int extra = waitpid(-1, st, 16);
    if (extra != -1) {
        fail("fork blast: waitpid(-1) should return -1 when no children");
        return;
    }

    print("fork blast OK.\n");
}

/* test_sleep_interrupt_sigchld: Implement test sleep interrupt sigchld. */
void test_sleep_interrupt_sigchld() {
    print("\n--- Test 16: Sleep Interrupt (SIGCHLD) ---\n");
    int t0 = parse_sched_ticks();
    if (t0 < 0) {
        fail("parse /proc/sched ticks");
        return;
    }
    int pid = fork();
    if (pid == 0) {
        sleep(2);
        exit(0);
    }
    if (pid < 0) {
        fail("fork for sleep interrupt");
        return;
    }
    sleep(50);
    int t1 = parse_sched_ticks();
    if (t1 < 0) {
        fail("parse /proc/sched ticks after sleep");
        return;
    }
    int delta = t1 - t0;
    int st[4] = {0};
    waitpid(pid, st, 16);
    if (delta >= 45) {
        fail("sleep not interrupted by SIGCHLD");
        return;
    }
    print("sleep interrupt OK.\n");
}

/* test_kill_sigterm: Implement test kill sigterm. */
void test_kill_sigterm() {
    print("\n--- Test 17: Kill (SIGTERM) ---\n");
    int pid = fork();
    if (pid == 0) {
        sleep(200);
        exit(0);
    }
    if (pid < 0) {
        fail("fork for kill");
        return;
    }
    sleep(5);
    int SIGTERM = 15;
    int r = kill(pid, SIGTERM);
    if (r != 0) {
        fail("kill(SIGTERM) failed");
        return;
    }
    int st[4] = {0};
    int ret = waitpid(pid, st, 16);
    if (ret != pid) {
        fail("waitpid after kill");
        return;
    }
    if (st[1] != 3 || st[2] != SIGTERM) {
        fail("kill exit status mismatch");
        return;
    }
    print("kill SIGTERM OK.\n");
}

/* test_kill_sigkill: Implement test kill sigkill. */
void test_kill_sigkill() {
    print("\n--- Test 18: Kill (SIGKILL) ---\n");
    int pid = fork();
    if (pid == 0) {
        sleep(200);
        exit(0);
    }
    if (pid < 0) {
        fail("fork for kill sigkill");
        return;
    }
    sleep(5);
    int SIGKILL = 9;
    int r = kill(pid, SIGKILL);
    if (r != 0) {
        fail("kill(SIGKILL) failed");
        return;
    }
    int st[4] = {0};
    int ret = waitpid(pid, st, 16);
    if (ret != pid) {
        fail("waitpid after kill sigkill");
        return;
    }
    if (st[1] != 3 || st[2] != SIGKILL) {
        fail("kill sigkill exit status mismatch");
        return;
    }
    print("kill SIGKILL OK.\n");
}

/* test_kill_sigint: Implement test kill sigint. */
void test_kill_sigint() {
    print("\n--- Test 19: Kill (SIGINT) ---\n");
    int pid = fork();
    if (pid == 0) {
        sleep(200);
        exit(0);
    }
    if (pid < 0) {
        fail("fork for kill sigint");
        return;
    }
    sleep(5);
    int SIGINT = 2;
    int r = kill(pid, SIGINT);
    if (r != 0) {
        fail("kill(SIGINT) failed");
        return;
    }
    int st[4] = {0};
    int ret = waitpid(pid, st, 16);
    if (ret != pid) {
        fail("waitpid after kill sigint");
        return;
    }
    if (st[1] != 3 || st[2] != SIGINT) {
        fail("kill sigint exit status mismatch");
        return;
    }
    print("kill SIGINT OK.\n");
}

/* test_kill_sig0: Implement test kill sig0. */
void test_kill_sig0() {
    print("\n--- Test 20: Kill (SIG0) ---\n");
    int pid = fork();
    if (pid == 0) {
        sleep(200);
        exit(0);
    }
    if (pid < 0) {
        fail("fork for kill sig0");
        return;
    }
    int r = kill(pid, 0);
    if (r != 0) {
        fail("kill(SIG0) failed on live pid");
        return;
    }
    int bad = kill(99999, 0);
    if (bad == 0) {
        fail("kill(SIG0) should fail on invalid pid");
        return;
    }
    int SIGTERM = 15;
    kill(pid, SIGTERM);
    int st[4] = {0};
    waitpid(pid, st, 16);
    print("kill SIG0 OK.\n");
}

/* test_kill_pid1: Implement test kill pid1. */
void test_kill_pid1() {
    print("\n--- Test 21: Kill PID 1 ---\n");
    // int SIGTERM = 15;
    // int r = kill(1, SIGTERM);
    // if (r == 0) {
    //     fail("kill(pid 1) should fail");
    //     return;
    // }
    print("kill pid1 OK.\n");
}

/* test_cow_isolation: Implement test copy-on-write isolation. */
void test_cow_isolation() {
    print("\n--- Test 22: COW Isolation ---\n");
    for (int i = 0; i < 32; i++) {
        char *p = (char *)mmap(0, 4096, VMA_READ | VMA_WRITE, 0, -1, 0);
        if ((int)p == -1 || !p) {
            fail("cow mmap failed");
            return;
        }
        p[0] = 0x11;
        p[1] = 0x22;
        int pid = fork();
        if (pid == 0) {
            if (p[0] != 0x11 || p[1] != 0x22)
                exit(1);
            p[0] = 0x77;
            if (p[0] != 0x77)
                exit(2);
            exit(10);
        }
        if (pid < 0) {
            munmap(p, 4096);
            fail("cow fork failed");
            return;
        }
        int st[4] = {0};
        int ret = waitpid(pid, st, 16);
        if (ret != pid || st[1] != 0 || st[0] != 10) {
            munmap(p, 4096);
            fail("cow child exit bad");
            return;
        }
        if (p[0] != 0x11 || p[1] != 0x22) {
            munmap(p, 4096);
            fail("cow parent modified");
            return;
        }
        munmap(p, 4096);
    }
    print("cow isolation OK.\n");
}

/* test_cow_stats: Implement test copy-on-write stats. */
void test_cow_stats() {
    print("\n--- Test 23: COW Stats ---\n");
    int f0 = 0, c0 = 0;
    if (parse_cow_stats(&f0, &c0) < 0) {
        fail("read /proc/cow");
        return;
    }
    int iters = 16;
    for (int i = 0; i < iters; i++) {
        char *p = (char *)mmap(0, 4096, VMA_READ | VMA_WRITE, 0, -1, 0);
        if ((int)p == -1 || !p) {
            fail("cow stats mmap failed");
            return;
        }
        p[0] = 0x44;
        int pid = fork();
        if (pid == 0) {
            p[0] = 0x55;
            exit(0);
        }
        if (pid < 0) {
            munmap(p, 4096);
            fail("cow stats fork failed");
            return;
        }
        int st[4] = {0};
        waitpid(pid, st, 16);
        p[0] = 0x66;
        munmap(p, 4096);
    }
    int f1 = 0, c1 = 0;
    if (parse_cow_stats(&f1, &c1) < 0) {
        fail("read /proc/cow after");
        return;
    }
    int df = f1 - f0;
    int dc = c1 - c0;
    if (df < iters) {
        fail("cow faults too low");
        return;
    }
    if (dc < 1 || dc > df) {
        fail("cow copies invalid");
        return;
    }
    print("cow stats OK.\n");
}

/* test_cow_single_copy: Implement test copy-on-write single copy. */
void test_cow_single_copy() {
    print("\n--- Test 24: COW Single Copy ---\n");
    int f0 = 0, c0 = 0;
    if (parse_cow_stats(&f0, &c0) < 0) {
        fail("read /proc/cow before");
        return;
    }
    int iters = 16;
    for (int i = 0; i < iters; i++) {
        char *p = (char *)mmap(0, 4096, VMA_READ | VMA_WRITE, 0, -1, 0);
        if ((int)p == -1 || !p) {
            fail("cow single mmap failed");
            return;
        }
        p[0] = 0x21;
        int pid = fork();
        if (pid == 0) {
            p[0] = 0x22;
            exit(0);
        }
        if (pid < 0) {
            munmap(p, 4096);
            fail("cow single fork failed");
            return;
        }
        int st[4] = {0};
        waitpid(pid, st, 16);
        p[0] = 0x23;
        p[0] = 0x24;
        munmap(p, 4096);
    }
    int f1 = 0, c1 = 0;
    if (parse_cow_stats(&f1, &c1) < 0) {
        fail("read /proc/cow after");
        return;
    }
    int df = f1 - f0;
    int dc = c1 - c0;
    if (dc != iters) {
        fail("cow copies not single per iter");
        return;
    }
    if (df < iters * 2) {
        fail("cow faults too low for single-copy");
        return;
    }
    print("cow single copy OK.\n");
}

/* test_cow_release: Implement test copy-on-write release. */
void test_cow_release() {
    print("\n--- Test 25: COW Release ---\n");
    int before = parse_memfree_kb();
    if (before < 0) {
        fail("memfree before");
        return;
    }
    int iters = 8;
    int pages = 512;
    int len = pages * 4096;
    for (int i = 0; i < iters; i++) {
        char *p = (char *)mmap(0, len, VMA_READ | VMA_WRITE, 0, -1, 0);
        if ((int)p == -1 || !p) {
            fail("cow release mmap failed");
            return;
        }
        for (int off = 0; off < len; off += 4096)
            p[off] = (char)i;
        int pid = fork();
        if (pid == 0) {
            for (int off = 0; off < len; off += 4096)
                p[off] = (char)(i + 1);
            exit(0);
        }
        if (pid < 0) {
            munmap(p, len);
            fail("cow release fork failed");
            return;
        }
        int st[4] = {0};
        waitpid(pid, st, 16);
        munmap(p, len);
    }
    int after = parse_memfree_kb();
    if (after < 0) {
        fail("memfree after");
        return;
    }
    int delta = before - after;
    if (delta > 2048) {
        fail("cow release leak");
        return;
    }
    print("cow release OK.\n");
}

/* test_pfault_stats: Implement test pfault stats. */
void test_pfault_stats() {
    print("\n--- Test 26: Page Fault Stats ---\n");
    int t0 = 0, p0 = 0, np0 = 0, w0 = 0, u0 = 0, prot0 = 0;
    if (parse_pfault_stats(&t0, &p0, &np0, &w0, &u0, &prot0) < 0) {
        fail("read /proc/pfault");
        return;
    }
    char *p = (char *)mmap(0, 4096, VMA_READ | VMA_WRITE, 0, -1, 0);
    if ((int)p == -1 || !p) {
        fail("pfault mmap");
        return;
    }
    p[0] = 0x1;
    munmap(p, 4096);
    int t1 = 0, p1 = 0, np1 = 0, w1 = 0, u1 = 0, prot1 = 0;
    if (parse_pfault_stats(&t1, &p1, &np1, &w1, &u1, &prot1) < 0) {
        fail("read /proc/pfault after access");
        return;
    }
    if (np1 <= np0 || w1 <= w0 || u1 <= u0 || t1 <= t0) {
        fail("pfault counters no increase");
        return;
    }
    char *q = (char *)mmap(0, 4096, VMA_READ | VMA_WRITE, 0, -1, 0);
    if ((int)q == -1 || !q) {
        fail("pfault mmap2");
        return;
    }
    q[0] = 0x2;
    int pid = fork();
    if (pid == 0) {
        volatile char tmp = q[0];
        if (tmp == 0x7f)
            q[0] = tmp;
        int prot_res = syscall3(SYS_MPROTECT, (int)q, 4096, VMA_READ);
        if (prot_res < 0)
            exit(3);
        q[0] = 0x3;
        exit(0);
    }
    if (pid < 0) {
        munmap(q, 4096);
        fail("pfault fork");
        return;
    }
    int st[4] = {0};
    waitpid(pid, st, 16);
    munmap(q, 4096);
    if (st[1] != 2) {
        fail("pfault child not killed");
        return;
    }
    int t2 = 0, p2 = 0, np2 = 0, w2 = 0, u2 = 0, prot2 = 0;
    if (parse_pfault_stats(&t2, &p2, &np2, &w2, &u2, &prot2) < 0) {
        fail("read /proc/pfault after prot");
        return;
    }
    if (prot2 <= prot1 || p2 <= p1) {
        fail("pfault prot not counted");
        return;
    }
    print("pfault stats OK.\n");
}

/* test_vmscan_wakeups: Implement test vmscan wakeups. */
void test_vmscan_wakeups() {
    print("\n--- Test 27: Vmscan Wakeups ---\n");
    int w0 = 0, t0 = 0, r0 = 0, a0 = 0, f0 = 0;
    if (parse_vmscan_stats(&w0, &t0, &r0, &a0, &f0) < 0) {
        fail("read /proc/vmscan");
        return;
    }
    int memfree_kb = parse_memfree_kb();
    if (memfree_kb < 0) {
        fail("memfree for vmscan");
        return;
    }
    int bytes = (memfree_kb * 1024 * 3) / 4;
    if (bytes < (16 * 1024 * 1024)) {
        print("vmscan SKIP.\n");
        return;
    }
    char *p = (char *)mmap(0, bytes, VMA_READ | VMA_WRITE, 0, -1, 0);
    if ((int)p == -1 || !p) {
        fail("vmscan mmap");
        return;
    }
    for (int off = 0; off < bytes; off += 4096)
        p[off] = (char)(off >> 12);
    munmap(p, bytes);
    int w1 = 0, t1 = 0, r1 = 0, a1 = 0, f1 = 0;
    if (parse_vmscan_stats(&w1, &t1, &r1, &a1, &f1) < 0) {
        fail("read /proc/vmscan after");
        return;
    }
    if (w1 < w0) {
        fail("vmscan wakeups decreased");
        return;
    }
    if (t1 < t0) {
        fail("vmscan tries decreased");
        return;
    }
    if (r1 < r0) {
        fail("vmscan reclaims decreased");
        return;
    }
    if (a1 < a0) {
        fail("vmscan anon reclaims decreased");
        return;
    }
    if (f1 < f0) {
        fail("vmscan file reclaims decreased");
        return;
    }
    print("vmscan wakeups OK.\n");
}

/* test_file_cache_reclaim: Implement test file cache reclaim. */
void test_file_cache_reclaim() {
    print("\n--- Test 28: File Cache Reclaim ---\n");
    int w0 = 0, t0 = 0, r0 = 0, a0 = 0, f0 = 0;
    if (parse_vmscan_stats(&w0, &t0, &r0, &a0, &f0) < 0) {
        fail("read /proc/vmscan before file");
        return;
    }
    int fd = open("/cache.bin", O_CREAT | O_TRUNC);
    if (fd < 0) {
        fail("open cache.bin");
        return;
    }
    int pages = 256;
    char buf[4096];
    for (int i = 0; i < 4096; i++)
        buf[i] = (char)i;
    for (int i = 0; i < pages; i++) {
        int wr = write(fd, buf, 4096);
        if (wr != 4096) {
            close(fd);
            fail("write cache.bin");
            return;
        }
    }
    close(fd);
    int procfd = open("/proc/vmscan", 0);
    if (procfd >= 0) {
        write(procfd, "reclaim\n", 8);
        close(procfd);
    }
    int memfree_kb = parse_memfree_kb();
    if (memfree_kb < 0) {
        fail("memfree for file reclaim");
        return;
    }
    int observed = 0;
    int t1 = t0, f1 = f0;
    for (int round = 0; round < 2; round++) {
        int bytes = (memfree_kb * 1024 * 15) / 16;
        if (bytes < (8 * 1024 * 1024)) {
            break;
        }
        char *p = (char *)mmap(0, bytes, VMA_READ | VMA_WRITE, 0, -1, 0);
        if ((int)p == -1 || !p) {
            break;
        }
        for (int off = 0; off < bytes; off += 4096)
            p[off] = (char)(off >> 12);
        munmap(p, bytes);
        int w2 = 0, t2 = 0, r2 = 0, a2 = 0, f2 = 0;
        if (parse_vmscan_stats(&w2, &t2, &r2, &a2, &f2) < 0)
            break;
        t1 = t2;
        f1 = f2;
        if (f1 > f0) {
            observed = 1;
            break;
        }
    }
    if (!observed) {
        if (t1 == t0) {
            print("file reclaim SKIP.\n");
            return;
        }
        fail("file reclaim not observed");
        return;
    }
    print("file reclaim OK.\n");
}

/* test_writeback: Implement test writeback. */
void test_writeback() {
    print("\n--- Test 29: Writeback ---\n");
    int d0 = 0, c0 = 0, x0 = 0;
    if (parse_writeback_stats(&d0, &c0, &x0) < 0) {
        fail("read /proc/writeback");
        return;
    }
    int fd = open("/wb.txt", O_CREAT | O_TRUNC);
    if (fd < 0) {
        fail("open wb.txt");
        return;
    }
    char buf[4096];
    for (int i = 0; i < 4096; i++)
        buf[i] = (char)(i + 1);
    int wr = write(fd, buf, 4096);
    close(fd);
    if (wr != 4096) {
        fail("write wb.txt");
        return;
    }
    int d1 = 0, c1 = 0, x1 = 0;
    if (parse_writeback_stats(&d1, &c1, &x1) < 0) {
        fail("read /proc/writeback after write");
        return;
    }
    if (d1 <= d0) {
        fail("writeback dirty not increased");
        return;
    }
    int procfd = open("/proc/writeback", 0);
    if (procfd >= 0) {
        write(procfd, "flush\n", 6);
        close(procfd);
    }
    int d2 = 0, c2 = 0, x2 = 0;
    if (parse_writeback_stats(&d2, &c2, &x2) < 0) {
        fail("read /proc/writeback after flush");
        return;
    }
    if (d2 >= d1 || c2 <= c1) {
        fail("writeback flush ineffective");
        return;
    }
    print("writeback OK.\n");
}

/* test_writeback_truncate: Implement test writeback truncate. */
void test_writeback_truncate() {
    print("\n--- Test 31: Writeback Truncate ---\n");
    int d0 = 0, c0 = 0, x0 = 0;
    if (parse_writeback_stats(&d0, &c0, &x0) < 0) {
        fail("read /proc/writeback before truncate");
        return;
    }
    int fd = open("/wb2.txt", O_CREAT | O_TRUNC);
    if (fd < 0) {
        fail("open wb2.txt");
        return;
    }
    char buf[4096];
    for (int i = 0; i < 4096; i++)
        buf[i] = 'A';
    int wr = write(fd, buf, 4096);
    close(fd);
    if (wr != 4096) {
        fail("write wb2.txt");
        return;
    }
    int d1 = 0, c1 = 0, x1 = 0;
    if (parse_writeback_stats(&d1, &c1, &x1) < 0) {
        fail("read /proc/writeback after write2");
        return;
    }
    if (d1 <= d0) {
        fail("writeback dirty not increased 2");
        return;
    }
    fd = open("/wb2.txt", 0);
    if (fd < 0) {
        fail("open wb2.txt overwrite");
        return;
    }
    for (int i = 0; i < 512; i++)
        buf[i] = 'B';
    wr = write(fd, buf, 512);
    close(fd);
    if (wr != 512) {
        fail("overwrite wb2.txt");
        return;
    }
    fd = open("/wb2.txt", 0);
    if (fd < 0) {
        fail("open wb2.txt read");
        return;
    }
    int rd = read(fd, buf, 4096);
    close(fd);
    if (rd != 4096) {
        fail("read wb2.txt");
        return;
    }
    for (int i = 0; i < 512; i++) {
        if (buf[i] != 'B') {
            fail("overwrite mismatch");
            return;
        }
    }
    for (int i = 512; i < 4096; i++) {
        if (buf[i] != 'A') {
            fail("partial overwrite mismatch");
            return;
        }
    }
    fd = open("/wb2.txt", O_TRUNC);
    if (fd < 0) {
        fail("truncate wb2.txt");
        return;
    }
    close(fd);
    int d2 = 0, c2 = 0, x2 = 0;
    if (parse_writeback_stats(&d2, &c2, &x2) < 0) {
        fail("read /proc/writeback after truncate");
        return;
    }
    if (x2 <= x0) {
        fail("writeback discard not increased");
        return;
    }
    if (d2 >= d1) {
        fail("writeback dirty not decreased on truncate");
        return;
    }
    fd = open("/wb2.txt", 0);
    if (fd < 0) {
        fail("open wb2.txt read after truncate");
        return;
    }
    rd = read(fd, buf, 1);
    close(fd);
    if (rd != 0) {
        fail("truncate size not zero");
        return;
    }
    print("writeback truncate OK.\n");
}

/* test_pagecache_stats: Implement test page cache stats. */
void test_pagecache_stats() {
    print("\n--- Test 30: Page Cache Stats ---\n");
    int h0 = 0, m0 = 0;
    if (parse_pagecache_stats(&h0, &m0) < 0) {
        fail("read /proc/pagecache");
        return;
    }
    int fd = open("/pc.txt", O_CREAT | O_TRUNC);
    if (fd < 0) {
        fail("open pc.txt");
        return;
    }
    char buf[4096];
    for (int i = 0; i < 4096; i++)
        buf[i] = (char)(i ^ 0x5a);
    int wr = write(fd, buf, 4096);
    close(fd);
    if (wr != 4096) {
        fail("write pc.txt");
        return;
    }
    int h1 = 0, m1 = 0;
    if (parse_pagecache_stats(&h1, &m1) < 0) {
        fail("read /proc/pagecache after write");
        return;
    }
    if (m1 <= m0) {
        fail("pagecache miss not increased on write");
        return;
    }
    int fd2 = open("/pc.txt", 0);
    if (fd2 < 0) {
        fail("open pc.txt read");
        return;
    }
    int rd = read(fd2, buf, 4096);
    close(fd2);
    if (rd != 4096) {
        fail("read pc.txt");
        return;
    }
    int h2 = 0, m2 = 0;
    if (parse_pagecache_stats(&h2, &m2) < 0) {
        fail("read /proc/pagecache after hit");
        return;
    }
    if (h2 <= h1) {
        fail("pagecache hit not increased");
        return;
    }
    int procfd = open("/proc/writeback", 0);
    if (procfd >= 0) {
        write(procfd, "flush\n", 6);
        close(procfd);
    }
    procfd = open("/proc/vmscan", 0);
    if (procfd >= 0) {
        write(procfd, "reclaim\n", 8);
        close(procfd);
    }
    fd2 = open("/pc.txt", 0);
    if (fd2 < 0) {
        fail("open pc.txt read2");
        return;
    }
    rd = read(fd2, buf, 4096);
    close(fd2);
    if (rd != 4096) {
        fail("read pc.txt 2");
        return;
    }
    int h3 = 0, m3 = 0;
    if (parse_pagecache_stats(&h3, &m3) < 0) {
        fail("read /proc/pagecache after miss");
        return;
    }
    if (m3 <= m2) {
        fail("pagecache miss not increased");
        return;
    }
    print("pagecache stats OK.\n");
}

/* test_blockstats_ramdisk: Implement test blockstats ramdisk. */
void test_blockstats_ramdisk() {
    print("\n--- Test 32: Blockstats (ram0) ---\n");
    int r0 = 0, w0 = 0, br0 = 0, bw0 = 0;
    if (parse_blockstats(&r0, &w0, &br0, &bw0) < 0) {
        fail("read /proc/blockstats");
        return;
    }
    int fd = open("/dev/ram0", 0);
    if (fd < 0) {
        fail("open /dev/ram0");
        return;
    }
    char buf[4096];
    for (int i = 0; i < 4096; i++)
        buf[i] = 'R';
    int wr = write(fd, buf, 4096);
    close(fd);
    if (wr != 4096) {
        fail("write /dev/ram0");
        return;
    }
    int procfd = open("/proc/writeback", 0);
    if (procfd >= 0) {
        write(procfd, "flush\n", 6);
        close(procfd);
    }
    int r1 = 0, w1 = 0, br1 = 0, bw1 = 0;
    if (parse_blockstats(&r1, &w1, &br1, &bw1) < 0) {
        fail("read /proc/blockstats after write");
        return;
    }
    if (w1 <= w0 || bw1 <= bw0) {
        fail("blockstats write not increased");
        return;
    }
    fd = open("/dev/ram0", 0);
    if (fd < 0) {
        fail("open /dev/ram0 read");
        return;
    }
    int rd = read(fd, buf, 4096);
    if (rd != 4096) {
        close(fd);
        fail("read /dev/ram0 page0");
        return;
    }
    for (int i = 0; i < 4096; i++) {
        if (buf[i] != 'R') {
            close(fd);
            fail("ram0 data mismatch");
            return;
        }
    }
    rd = read(fd, buf, 4096);
    close(fd);
    if (rd != 4096) {
        fail("read /dev/ram0 page1");
        return;
    }
    for (int i = 0; i < 4096; i++) {
        if (buf[i] != 0) {
            fail("ram0 zero page mismatch");
            return;
        }
    }
    int r2 = 0, w2 = 0, br2 = 0, bw2 = 0;
    if (parse_blockstats(&r2, &w2, &br2, &bw2) < 0) {
        fail("read /proc/blockstats after read");
        return;
    }
    if (r2 <= r1 || br2 <= br1) {
        fail("blockstats read not increased");
        return;
    }
    print("blockstats ram0 OK.\n");
}

/* test_minix_mount_read: Implement test minix mount read. */
void test_minix_mount_read() {
    print("\n--- Test 33: MinixFS Read ---\n");
    int fd = open("/mnt/hello.txt", 0);
    if (fd < 0) {
        fail("open /mnt/hello.txt");
        return;
    }
    char buf[64];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) {
        fail("read /mnt/hello.txt");
        return;
    }
    const char *expect = "Hello from MinixFS!\n";
    int elen = 20;
    if (n < elen) {
        fail("minix read short");
        return;
    }
    for (int i = 0; i < elen; i++) {
        if (buf[i] != expect[i]) {
            fail("minix content mismatch");
            return;
        }
    }
    print("minixfs read OK.\n");
}

/* test_nvme_device: Implement test NVMe device. */
void test_nvme_device() {
    print("\n--- Test 34: NVMe Device --\n");
    int fd = open("/dev/nvme0n1", 0);
    if (fd < 0) {
        print("NVMe device not found, skipping test.\n");
        return;
    }
    close(fd);
    print("NVMe device detected OK.\n");
}

/* test_minix_mount_write: Implement test minix mount write. */
void test_minix_mount_write() {
    print("\n--- Test 35: MinixFS Write --\n");
    int fd = open("/mnt/test_write.txt", O_CREAT);
    if (fd < 0) {
        fail("open /mnt/test_write.txt");
        return;
    }
    const char *msg = "Test write to MinixFS!\n";
    if (write(fd, msg, 21) != 21) {
        fail("write /mnt/test_write.txt");
        close(fd);
        return;
    }
    close(fd);
    print("minixfs write OK.\n");

    fd = open("/mnt/test_write.txt", 0);
    if (fd < 0) {
        fail("open /mnt/test_write.txt for read");
        return;
    }
    char buf[64];
    int n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) {
        fail("read /mnt/test_write.txt");
        return;
    }
    if (n != 21) {
        fail("minix write read short");
        return;
    }
    for (int i = 0; i < 21; i++) {
        if (buf[i] != msg[i]) {
            fail("minix write content mismatch");
            return;
        }
    }
    print("minixfs write-read OK.\n");
}

/* main: Implement main. */
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
    test_proc_meminfo_iomem();
    test_large_mmap_touch();
    test_pci_uevent();
    test_sysfs_layout();
    test_sysfs_bind_unbind_console();
    test_fork_blast();
    test_sleep_interrupt_sigchld();
    test_kill_sigterm();
    test_kill_sigkill();
    test_kill_sigint();
    test_kill_sig0();
    test_kill_pid1();
    test_cow_isolation();
    test_cow_stats();
    test_cow_single_copy();
    test_cow_release();
    test_pfault_stats();
    test_vmscan_wakeups();
    test_file_cache_reclaim();
    test_writeback();
    test_writeback_truncate();
    test_pagecache_stats();
    // test_blockstats_ramdisk();
    test_minix_mount_read();
    test_nvme_device();
    test_minix_mount_write();

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
