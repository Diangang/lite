#include "shell.h"
#include "libc.h"
#include "timer.h"
#include "task.h"
#include "syscall.h"
#include "pmm.h"
#include "kheap.h"
#include "vfs.h"
#include "file.h"
#include "tty.h"
#include "vga.h"
#include "console.h"

#define CMD_BUF_SIZE 256

static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_index = 0;
static int foreground_is_user = 0;
static uint32_t foreground_pid = 0;

static void shell_prompt(void)
{
    printf("lite-os> ");
}

static char shell_getchar_blocking(void)
{
    char c = 0;
    while (tty_read_blocking(&c, 1) == 0) {
    }
    return c;
}

static void shell_execute(void)
{
    /* Null terminate the string */
    cmd_buffer[cmd_index] = '\0';

    if (cmd_index == 0) {
        /* Empty command */
    }
    else if (strcmp(cmd_buffer, "help") == 0) {
        printf("Available commands:\n");
        printf("  help    - Show this message\n");
        printf("  clear   - Clear the screen\n");
        printf("  hello   - Print a greeting\n");
        printf("  info    - Display system information\n");
        printf("  echo    - Print text to the screen\n");
        printf("  uptime  - Show system uptime\n");
        printf("  meminfo - Show physical memory map\n");
        printf("  alloc   - Test physical memory allocation\n");
        printf("  vmmtest - Trigger a Page Fault\n");
        printf("  heaptest- Test kernel heap (malloc/free)\n");
        printf("  demo on - Enable task demo output\n");
        printf("  demo off- Disable task demo output\n");
        printf("  yield   - Voluntary scheduler yield\n");
        printf("  sleep   - Sleep for 50 ticks\n");
        printf("  ps      - List tasks\n");
        printf("  syscall - Test syscall write/yield\n");
        printf("  user    - Run user.elf\n");
        printf("  run     - Run a user ELF from initrd\n");
        printf("  ls      - List files in initrd\n");
        printf("  cat     - Print file content\n");
    }
    else if (strcmp(cmd_buffer, "clear") == 0) {
        init_vga();
    }
    else if (strcmp(cmd_buffer, "hello") == 0) {
        printf("Hello from the Lite OS Shell!\n");
    }
    else if (strcmp(cmd_buffer, "info") == 0) {
        printf("Lite OS v0.1\n");
        printf("Architecture: x86 (32-bit)\n");
        printf("Features: GDT, IDT, PIC, PIT, PS/2 Keyboard, PMM\n");
    }
    else if (strcmp(cmd_buffer, "uptime") == 0) {
        uint32_t uptime_sec = timer_get_uptime();
        uint32_t ticks = timer_get_ticks();
        printf("System uptime: %d seconds (%d ticks)\n", uptime_sec, ticks);
    }
    else if (strcmp(cmd_buffer, "meminfo") == 0) {
        pmm_print_info();
    }
    else if (strcmp(cmd_buffer, "pwd") == 0) {
        printf("%s\n", vfs_getcwd());
    }
    else if (strcmp(cmd_buffer, "cd") == 0 || strncmp(cmd_buffer, "cd ", 3) == 0) {
        const char *path = "/";
        if (strncmp(cmd_buffer, "cd ", 3) == 0) {
            path = cmd_buffer + 3;
            while (*path == ' ') path++;
            if (*path == 0) path = "/";
        }
        if (vfs_chdir(path) != 0) {
            printf("cd: %s: No such file or directory\n", path);
        }
    }
    else if (strncmp(cmd_buffer, "mkdir ", 6) == 0) {
        const char *path = cmd_buffer + 6;
        while (*path == ' ') path++;
        if (!*path) {
            printf("mkdir: missing operand\n");
        } else if (vfs_mkdir(path) != 0)
            printf("mkdir: %s\n", path);
    }
    else if (strncmp(cmd_buffer, "touch ", 6) == 0) {
        const char *path = cmd_buffer + 6;
        while (*path == ' ') path++;
        if (!*path) {
            printf("touch: missing operand\n");
        } else {
            struct file *f = file_open_path(path, O_CREAT);
            if (!f)
                printf("touch: %s\n", path);
            else
                file_close(f);
        }
    }
    else if (strncmp(cmd_buffer, "writefile ", 10) == 0) {
        char *p = cmd_buffer + 10;
        while (*p == ' ') p++;
        if (!*p) {
            printf("writefile: missing operand\n");
        } else {
            char *sp = p;
            while (*sp && *sp != ' ') sp++;
            if (!*sp) {
                printf("writefile: missing data\n");
            } else {
                *sp = 0;
                const char *path = p;
                const char *data = sp + 1;
                while (*data == ' ') data++;
                struct file *f = file_open_path(path, O_CREAT | O_TRUNC);
                if (!f)
                    printf("writefile: %s\n", path);
                else {
                    file_write(f, (const uint8_t*)data, (uint32_t)strlen(data));
                    file_write(f, (const uint8_t*)"\n", 1);
                    file_close(f);
                }
            }
        }
    }
    else if (strcmp(cmd_buffer, "alloc") == 0) {
        printf("Allocating 3 pages...\n");
        void* p1 = pmm_alloc_page();
        printf("Page 1: 0x%x\n", (uint32_t)p1);
        void* p2 = pmm_alloc_page();
        printf("Page 2: 0x%x\n", (uint32_t)p2);
        void* p3 = pmm_alloc_page();
        printf("Page 3: 0x%x\n", (uint32_t)p3);

        printf("Freeing Page 2...\n");
        pmm_free_page(p2);

        void* p4 = pmm_alloc_page();
        printf("Allocated Page 4 (should be same as Page 2): 0x%x\n", (uint32_t)p4);
    }
    else if (strcmp(cmd_buffer, "vmmtest") == 0) {
        printf("Attempting to write to unmapped memory (0xA0000000)...\n");
        uint32_t *ptr = (uint32_t*)0xA0000000;
        *ptr = 0xDEADBEEF;
    }
    else if (strcmp(cmd_buffer, "heaptest") == 0) {
        kheap_print_stats();

        printf("Allocating 128 bytes...\n");
        void *a = kmalloc(128);
        printf("Allocated at 0x%x\n", (uint32_t)a);

        printf("Allocating 256 bytes...\n");
        void *b = kmalloc(256);
        printf("Allocated at 0x%x\n", (uint32_t)b);

        kheap_print_stats();

        printf("Freeing first block...\n");
        kfree(a);

        kheap_print_stats();

        printf("Allocating 64 bytes (should fit in first block)...\n");
        void *c = kmalloc(64);
        printf("Allocated at 0x%x\n", (uint32_t)c);

        kheap_print_stats();
    }
    else if (strcmp(cmd_buffer, "demo on") == 0) {
        task_set_demo_enabled(1);
        printf("Task demo enabled.\n");
    }
    else if (strcmp(cmd_buffer, "demo off") == 0) {
        task_set_demo_enabled(0);
        printf("Task demo disabled.\n");
    }
    else if (strcmp(cmd_buffer, "demo") == 0) {
        if (task_get_demo_enabled()) {
            printf("Task demo is enabled.\n");
        } else {
            printf("Task demo is disabled.\n");
        }
    }
    else if (strcmp(cmd_buffer, "yield") == 0) {
        printf("Yielding...\n");
        task_yield();
    }
    else if (strcmp(cmd_buffer, "sleep") == 0) {
        printf("Sleeping for 50 ticks...\n");
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
            : "a"(SYS_WRITE), "b"(1), "c"(msg), "d"(len)
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
        int pid = task_create_user("user.elf");
        if (pid < 0) {
            printf("Failed to create user task.\n");
            shell_set_foreground(0);
        } else {
            shell_set_foreground_pid((uint32_t)pid);
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
            printf("usage: run <file>\n");
        } else {
            int pid = task_create_user(filename);
            if (pid < 0) {
                printf("Failed to create user task.\n");
                shell_set_foreground(0);
            } else {
                shell_set_foreground_pid((uint32_t)pid);
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
        struct vfs_inode *dir = vfs_resolve(".");
        if (strncmp(cmd_buffer, "ls ", 3) == 0) {
            char *path = cmd_buffer + 3;
            if (path[0]) {
                struct vfs_inode *found = vfs_resolve(path);
                if (!found || ((found->flags & 0x7) != FS_DIRECTORY)) {
                    printf("Directory not found: %s\n", path);
                    cmd_index = 0;
                    return;
                }
                dir = found;
            }
        }
        if (!dir) {
            printf("No file system mounted or directory not found!\n");
            cmd_index = 0;
            return;
        }
        int i = 0;
        struct dirent *node = 0;
        while ((node = readdir_fs(dir, i)) != 0) {
            printf("Found file: %s\n", node->name);
            struct vfs_inode *fsnode = finddir_fs(dir, node->name);
            if ((fsnode->flags & 0x7) == FS_DIRECTORY)
                printf("\t(directory)\n");
            else
                printf("\t(file)\n");
            i++;
        }
    }
    else if (strncmp(cmd_buffer, "cat ", 4) == 0) {
        char* filename = cmd_buffer + 4;
        struct file *f = file_open_path(filename, 0);
        if (!f) {
            printf("File not found: %s\n", filename);
            cmd_index = 0;
            return;
        }
        if ((f->node->flags & 0x7) == FS_DIRECTORY) {
            printf("Not a file: %s\n", filename);
            file_close(f);
            cmd_index = 0;
            return;
        }
        uint8_t buf[256];
        while (1) {
            uint32_t n = file_read(f, buf, sizeof(buf));
            if (n == 0) break;
            for (uint32_t i = 0; i < n; i++) {
                printf("%c", (char)buf[i]);
            }
        }
        printf("\n");
        file_close(f);
    }
    else if (strncmp(cmd_buffer, "echo ", 5) == 0) {
        /* Print everything after 'echo ' */
        printf(cmd_buffer + 5);
        printf("\n");
    }
    else {
        printf("Unknown command: ");
        printf(cmd_buffer);
        printf("\n");
    }

    /* Reset buffer for next command */
    cmd_index = 0;
}

void init_shell(void)
{
    cmd_index = 0;
    foreground_is_user = 0;
    foreground_pid = 0;
    tty_init();
    tty_set_user_exit_hook(shell_on_user_exit);
    tty_set_flags(0);
}

void shell_set_foreground(int is_user)
{
    foreground_is_user = is_user ? 1 : 0;
    cmd_index = 0;
    if (!foreground_is_user) {
        foreground_pid = 0;
        tty_set_foreground_pid(0);
        tty_set_flags(0);
    } else {
        tty_set_foreground_pid(foreground_pid);
        tty_set_flags(TTY_FLAG_ECHO | TTY_FLAG_CANON);
    }
}

void shell_process_char(char c)
{
    tty_receive_char(c);
}

uint32_t shell_read(char *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    return 0;
}

uint32_t shell_read_blocking(char *buf, uint32_t len)
{
    return tty_read_blocking(buf, len);
}

uint32_t shell_tty_get_flags(void)
{
    return tty_get_flags();
}

void shell_tty_set_flags(uint32_t flags)
{
    tty_set_flags(flags);
}

void shell_set_foreground_pid(uint32_t pid)
{
    foreground_pid = pid;
    tty_set_foreground_pid(pid);
    shell_set_foreground(pid ? 1 : 0);
}

uint32_t shell_get_foreground_pid(void)
{
    return foreground_pid;
}

uint32_t shell_tty_read_blocking(char *buf, uint32_t len)
{
    return tty_read_blocking(buf, len);
}

void shell_on_user_exit(void)
{
    shell_set_foreground(0);
    printf("\n");
    shell_prompt();
}

void shell_task(void)
{
    printf("\n--- Lite OS Shell Started ---\n");
    if (!foreground_is_user) {
        shell_prompt();
    }
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
            printf("\n");
            shell_execute();
            if (!foreground_is_user) {
                shell_prompt();
            }
        } else if (c == '\b' || c == 0x7F) {
            if (cmd_index > 0) {
                cmd_index--;
                printf("\b");
            }
        } else {
            if (cmd_index < CMD_BUF_SIZE - 1) {
                cmd_buffer[cmd_index++] = c;
                printf("%c", c);
            }
        }
    }
}
