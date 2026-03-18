#include "shell.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"
#include "task.h"
#include "syscall.h"
#include "pmm.h"
#include "kheap.h"
#include "fs.h"

#define CMD_BUF_SIZE 256
#define INPUT_BUF_SIZE 256

static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_index = 0;
static char input_buffer[INPUT_BUF_SIZE];
static uint32_t input_head = 0;
static uint32_t input_tail = 0;
static uint32_t input_count = 0;
static int foreground_is_user = 0;
static wait_queue_t input_waitq;

static uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

static void shell_prompt(void)
{
    terminal_writestring("lite-os> ");
}

static char shell_getchar_blocking(void)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (input_count > 0) {
            char c = input_buffer[input_tail];
            input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
            input_count--;
            irq_restore(flags);
            return c;
        }
        wait_queue_block_locked(&input_waitq);
        irq_restore(flags);
        task_yield();
    }
}

static void shell_execute(void)
{
    /* Null terminate the string */
    cmd_buffer[cmd_index] = '\0';

    if (cmd_index == 0) {
        /* Empty command */
    }
    else if (strcmp(cmd_buffer, "help") == 0) {
        terminal_writestring("Available commands:\n");
        terminal_writestring("  help    - Show this message\n");
        terminal_writestring("  clear   - Clear the screen\n");
        terminal_writestring("  hello   - Print a greeting\n");
        terminal_writestring("  info    - Display system information\n");
        terminal_writestring("  echo    - Print text to the screen\n");
        terminal_writestring("  uptime  - Show system uptime\n");
        terminal_writestring("  meminfo - Show physical memory map\n");
        terminal_writestring("  alloc   - Test physical memory allocation\n");
        terminal_writestring("  vmmtest - Trigger a Page Fault\n");
        terminal_writestring("  heaptest- Test kernel heap (malloc/free)\n");
        terminal_writestring("  demo on - Enable task demo output\n");
        terminal_writestring("  demo off- Disable task demo output\n");
        terminal_writestring("  yield   - Voluntary scheduler yield\n");
        terminal_writestring("  sleep   - Sleep for 50 ticks\n");
        terminal_writestring("  ps      - List tasks\n");
        terminal_writestring("  syscall - Test syscall write/yield\n");
        terminal_writestring("  user    - Run user.elf\n");
        terminal_writestring("  run     - Run a user ELF from initrd\n");
        terminal_writestring("  ls      - List files in initrd\n");
        terminal_writestring("  cat     - Print file content\n");
    }
    else if (strcmp(cmd_buffer, "clear") == 0) {
        terminal_initialize();
    }
    else if (strcmp(cmd_buffer, "hello") == 0) {
        terminal_writestring("Hello from the Lite OS Shell!\n");
    }
    else if (strcmp(cmd_buffer, "info") == 0) {
        terminal_writestring("Lite OS v0.1\n");
        terminal_writestring("Architecture: x86 (32-bit)\n");
        terminal_writestring("Features: GDT, IDT, PIC, PIT, PS/2 Keyboard, PMM\n");
    }
    else if (strcmp(cmd_buffer, "uptime") == 0) {
        uint32_t uptime_sec = timer_get_uptime();
        uint32_t ticks = timer_get_ticks();
        printf("System uptime: %d seconds (%d ticks)\n", uptime_sec, ticks);
    }
    else if (strcmp(cmd_buffer, "meminfo") == 0) {
        pmm_print_memory_map();
    }
    else if (strcmp(cmd_buffer, "alloc") == 0) {
        terminal_writestring("Allocating 3 pages...\n");
        void* p1 = pmm_alloc_page();
        printf("Page 1: 0x%x\n", (uint32_t)p1);
        void* p2 = pmm_alloc_page();
        printf("Page 2: 0x%x\n", (uint32_t)p2);
        void* p3 = pmm_alloc_page();
        printf("Page 3: 0x%x\n", (uint32_t)p3);

        terminal_writestring("Freeing Page 2...\n");
        pmm_free_page(p2);

        void* p4 = pmm_alloc_page();
        printf("Allocated Page 4 (should be same as Page 2): 0x%x\n", (uint32_t)p4);
    }
    else if (strcmp(cmd_buffer, "vmmtest") == 0) {
        terminal_writestring("Attempting to write to unmapped memory (0xA0000000)...\n");
        uint32_t *ptr = (uint32_t*)0xA0000000;
        *ptr = 0xDEADBEEF;
    }
    else if (strcmp(cmd_buffer, "heaptest") == 0) {
        kheap_print_stats();

        terminal_writestring("Allocating 128 bytes...\n");
        void *a = kmalloc(128);
        printf("Allocated at 0x%x\n", (uint32_t)a);

        terminal_writestring("Allocating 256 bytes...\n");
        void *b = kmalloc(256);
        printf("Allocated at 0x%x\n", (uint32_t)b);

        kheap_print_stats();

        terminal_writestring("Freeing first block...\n");
        kfree(a);

        kheap_print_stats();

        terminal_writestring("Allocating 64 bytes (should fit in first block)...\n");
        void *c = kmalloc(64);
        printf("Allocated at 0x%x\n", (uint32_t)c);

        kheap_print_stats();
    }
    else if (strcmp(cmd_buffer, "demo on") == 0) {
        task_set_demo_enabled(1);
        terminal_writestring("Task demo enabled.\n");
    }
    else if (strcmp(cmd_buffer, "demo off") == 0) {
        task_set_demo_enabled(0);
        terminal_writestring("Task demo disabled.\n");
    }
    else if (strcmp(cmd_buffer, "demo") == 0) {
        if (task_get_demo_enabled()) {
            terminal_writestring("Task demo is enabled.\n");
        } else {
            terminal_writestring("Task demo is disabled.\n");
        }
    }
    else if (strcmp(cmd_buffer, "yield") == 0) {
        terminal_writestring("Yielding...\n");
        task_yield();
    }
    else if (strcmp(cmd_buffer, "sleep") == 0) {
        terminal_writestring("Sleeping for 50 ticks...\n");
        task_sleep(50);
        task_yield();
    }
    else if (strcmp(cmd_buffer, "ps") == 0) {
        task_list();
    }
    else if (strcmp(cmd_buffer, "syscall") == 0) {
        const char *msg = "syscall write ok\n";
        uint32_t len = strlen(msg);
        __asm__ volatile(
            "int $0x80"
            :
            : "a"(SYS_WRITE), "b"(msg), "c"(len)
            : "memory"
        );
        __asm__ volatile(
            "int $0x80"
            :
            : "a"(SYS_YIELD)
            : "memory"
        );
    }
    else if (strcmp(cmd_buffer, "user") == 0) {
        shell_set_foreground(1);
        int pid = task_create_user("user.elf");
        if (pid < 0) {
            terminal_writestring("Failed to create user task.\n");
            shell_set_foreground(0);
        } else {
            int code = 0;
            int reason = 0;
            uint32_t info0 = 0;
            uint32_t info1 = 0;
            task_wait((uint32_t)pid, &code, &reason, &info0, &info1);
            printf("exit: code=%d reason=%d info0=0x%x info1=0x%x\n", code, reason, info0, info1);
        }
    }
    else if (strncmp(cmd_buffer, "run ", 4) == 0) {
        char *filename = cmd_buffer + 4;
        if (!filename[0]) {
            terminal_writestring("usage: run <file>\n");
        } else {
            shell_set_foreground(1);
            int pid = task_create_user(filename);
            if (pid < 0) {
                terminal_writestring("Failed to create user task.\n");
                shell_set_foreground(0);
            } else {
                int code = 0;
                int reason = 0;
                uint32_t info0 = 0;
                uint32_t info1 = 0;
                task_wait((uint32_t)pid, &code, &reason, &info0, &info1);
                printf("exit: code=%d reason=%d info0=0x%x info1=0x%x\n", code, reason, info0, info1);
            }
        }
    }
    else if (strcmp(cmd_buffer, "ls") == 0 || strncmp(cmd_buffer, "ls ", 3) == 0) {
        if (!fs_root) {
            terminal_writestring("No file system mounted!\n");
        } else {
            fs_node_t *dir = fs_root;
            if (strncmp(cmd_buffer, "ls ", 3) == 0) {
                char *path = cmd_buffer + 3;
                if (path[0]) {
                    fs_node_t *found = finddir_fs(fs_root, path);
                    if (!found || ((found->flags & 0x7) != FS_DIRECTORY)) {
                        printf("Directory not found: %s\n", path);
                        cmd_index = 0;
                        return;
                    }
                    dir = found;
                }
            }
            int i = 0;
            struct dirent *node = 0;
            while ((node = readdir_fs(dir, i)) != 0) {
                printf("Found file: %s\n", node->name);
                fs_node_t *fsnode = finddir_fs(dir, node->name);
                if ((fsnode->flags & 0x7) == FS_DIRECTORY)
                    printf("\t(directory)\n");
                else
                    printf("\t(file)\n");
                i++;
            }
        }
    }
    else if (strncmp(cmd_buffer, "cat ", 4) == 0) {
        if (!fs_root) {
            terminal_writestring("No file system mounted!\n");
        } else {
            char* filename = cmd_buffer + 4;
            fs_node_t *fsnode = finddir_fs(fs_root, filename);
            if (fsnode) {
                uint8_t *buf = (uint8_t*)kmalloc(fsnode->length + 1);
                uint32_t sz = read_fs(fsnode, 0, fsnode->length, buf);
                buf[sz] = 0;
                terminal_writestring((char*)buf);
                terminal_writestring("\n");
                kfree(buf);
            } else {
                printf("File not found: %s\n", filename);
            }
        }
    }
    else if (strncmp(cmd_buffer, "echo ", 5) == 0) {
        /* Print everything after 'echo ' */
        terminal_writestring(cmd_buffer + 5);
        terminal_writestring("\n");
    }
    else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(cmd_buffer);
        terminal_writestring("\n");
    }

    /* Reset buffer for next command */
    cmd_index = 0;
}

void shell_init(void)
{
    cmd_index = 0;
    input_head = input_tail = input_count = 0;
    foreground_is_user = 0;
    wait_queue_init(&input_waitq);
}

void shell_set_foreground(int is_user)
{
    foreground_is_user = is_user ? 1 : 0;
    cmd_index = 0;
    input_head = input_tail = input_count = 0;
}

void shell_process_char(char c)
{
    if (c == '\r') {
        c = '\n';
    }
    uint32_t flags = irq_save();
    if (input_count < INPUT_BUF_SIZE) {
        input_buffer[input_head] = c;
        input_head = (input_head + 1) % INPUT_BUF_SIZE;
        input_count++;
        wait_queue_wake_all(&input_waitq);
    }
    irq_restore(flags);
}

uint32_t shell_read(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    uint32_t read = 0;
    uint32_t flags = irq_save();
    while (read < len && input_count > 0) {
        buf[read++] = input_buffer[input_tail];
        input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
        input_count--;
    }
    irq_restore(flags);
    return read;
}

uint32_t shell_read_blocking(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    for (;;) {
        uint32_t read = 0;
        uint32_t flags = irq_save();
        while (read < len && input_count > 0) {
            buf[read++] = input_buffer[input_tail];
            input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
            input_count--;
        }
        if (read > 0) {
            irq_restore(flags);
            return read;
        }
        wait_queue_block_locked(&input_waitq);
        irq_restore(flags);
        task_yield();
    }
}

void shell_on_user_exit(void)
{
    shell_set_foreground(0);
}

void shell_task(void)
{
    terminal_writestring("\n--- Lite OS Shell Started ---\n");
    shell_prompt();
    for (;;) {
        if (foreground_is_user) {
            task_yield();
            continue;
        }
        char c = shell_getchar_blocking();
        if (foreground_is_user) {
            continue;
        }
        if (c == '\r') c = '\n';
        if (c == '\n') {
            terminal_putchar('\n');
            shell_execute();
            if (!foreground_is_user) {
                shell_prompt();
            }
        } else if (c == '\b' || c == 0x7F) {
            if (cmd_index > 0) {
                cmd_index--;
                terminal_putchar('\b');
            }
        } else {
            if (cmd_index < CMD_BUF_SIZE - 1) {
                cmd_buffer[cmd_index++] = c;
                terminal_putchar(c);
            }
        }
    }
}
