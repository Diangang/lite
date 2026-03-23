#include "ulib.h"

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
    if (ret == 0) {
        print("munmap succeeded.\n");
    } else {
        print("munmap failed!\n");
    }
}

void test_bad_ptr() {
    print("\n--- Test 5: Bad Pointer in Syscall ---\n");
    print("Passing invalid pointer (0x08000000) to write()...\n");
    int ret = write(1, (void*)0x08000000, 4);
    if (ret == -1) {
        print("Kernel correctly rejected bad pointer (returned -1).\n");
    } else {
        print("FAIL: Kernel did not reject bad pointer! Returned: ");
        print_int(ret);
        print("\n");
    }
}

int main() {
    print("================================\n");
    print("  Lite OS Automated Test Suite  \n");
    print("================================\n");
    
    test_fork();
    test_file_io();
    test_pf();
    test_mmap();
    test_bad_ptr();
    
    print("\n================================\n");
    print("  All tests completed!          \n");
    print("================================\n");
    return 0;
}
