#include "shell.h"
#include "kernel.h"
#include "libc.h"
#include "timer.h"

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
        terminal_writestring("  help   - Show this message\n");
        terminal_writestring("  clear  - Clear the screen\n");
        terminal_writestring("  hello  - Print a greeting\n");
        terminal_writestring("  info   - Display system information\n");
        terminal_writestring("  echo   - Print text to the screen\n");
        terminal_writestring("  uptime - Show system uptime\n");
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
        terminal_writestring("Features: GDT, IDT, PIC, PIT, PS/2 Keyboard\n");
    }
    else if (strcmp(cmd_buffer, "uptime") == 0) {
        uint32_t uptime_sec = timer_get_uptime();
        uint32_t ticks = timer_get_ticks();
        printf("System uptime: %d seconds (%d ticks)\n", uptime_sec, ticks);
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