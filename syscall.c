#include "syscall.h"
#include "kernel.h"
#include "task.h"
#include "shell.h"
#include "vmm.h"

void syscall_init(void)
{
    register_interrupt_handler(128, syscall_handler);
}

static int syscall_write_user(const char *buf_user, uint32_t len)
{
    char tmp[128];
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        if (vmm_copyin(tmp, buf_user + off, chunk) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            terminal_putchar(tmp[i]);
        }
        off += chunk;
    }
    return (int)len;
}

static int syscall_write_kernel(const char *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
    }
    return (int)len;
}

static int syscall_read_user(char *buf_user, uint32_t len)
{
    if (!buf_user || len == 0) return 0;
    if (len > 256) len = 256;

    char tmp[256];
    uint32_t total = 0;
    while (total == 0) {
        total = shell_read(tmp, len);
        if (total == 0) {
            task_yield();
        }
    }
    if (vmm_copyout(buf_user, tmp, total) != 0) {
        return -1;
    }
    return (int)total;
}

static int syscall_read_kernel(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    uint32_t total = 0;
    while (total == 0) {
        total = shell_read(buf, len);
        if (total == 0) {
            task_yield();
        }
    }
    return (int)total;
}

void syscall_handler(registers_t *regs)
{
    int from_user = (regs->cs & 0x3) == 0x3;
    if (regs->eax == SYS_WRITE) {
        if (regs->ecx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, regs->ecx, 0)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_READ) {
        if (regs->ecx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, regs->ecx, 1)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    }

    switch (regs->eax) {
        case SYS_WRITE:
            if (from_user) {
                regs->eax = (uint32_t)syscall_write_user((const char *)regs->ebx, regs->ecx);
            } else {
                regs->eax = (uint32_t)syscall_write_kernel((const char *)regs->ebx, regs->ecx);
            }
            break;
        case SYS_YIELD:
            task_yield();
            regs->eax = 0;
            break;
        case SYS_SLEEP:
            task_sleep(regs->ebx);
            task_yield();
            regs->eax = 0;
            break;
        case SYS_EXIT:
            if (from_user) {
                task_exit_with_status((int)regs->ebx);
            } else {
                task_exit();
            }
            regs->eax = 0;
            break;
        case SYS_READ:
            if (from_user) {
                regs->eax = (uint32_t)syscall_read_user((char *)regs->ebx, regs->ecx);
            } else {
                regs->eax = (uint32_t)syscall_read_kernel((char *)regs->ebx, regs->ecx);
            }
            break;
        case SYS_GETPID:
            regs->eax = task_get_current_id();
            break;
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
