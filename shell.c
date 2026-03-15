#include "shell.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"
#include "pmm.h"
#include "kheap.h"

#define CMD_BUF_SIZE 256

static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_index = 0;

static void shell_prompt(void)
{
    terminal_writestring("lite-os> ");
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
        *ptr = 0xDEADBEEF; /* This should trigger a Page Fault! */
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
    terminal_writestring("\n--- Lite OS Shell Started ---\n");
    shell_prompt();
}

void shell_process_char(char c)
{
    if (c == '\n') {
        /* Enter key */
        terminal_putchar('\n');
        shell_execute();
        shell_prompt();
    }
    else if (c == '\b') {
        /* Backspace */
        if (cmd_index > 0) {
            cmd_index--;
            terminal_putchar('\b'); /* Visually erase on screen */
        }
    }
    else {
        /* Printable character */
        if (cmd_index < CMD_BUF_SIZE - 1) {
            cmd_buffer[cmd_index++] = c;
            terminal_putchar(c); /* Echo to screen */
        }
    }
}