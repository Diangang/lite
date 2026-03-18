#include "syscall.h"
#include "kernel.h"
#include "task.h"
#include "shell.h"

void syscall_init(void)
{
    register_interrupt_handler(128, syscall_handler);
}

static void syscall_write(const char *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
    }
}

static uint32_t syscall_read(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    uint32_t total = 0;
    while (total == 0) {
        total = shell_read(buf, len);
        if (total == 0) {
            task_yield();
        }
    }
    return total;
}

void syscall_handler(registers_t *regs)
{
    switch (regs->eax) {
        case SYS_WRITE:
            syscall_write((const char *)regs->ebx, regs->ecx);
            regs->eax = regs->ecx;
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
            task_exit();
            regs->eax = 0;
            break;
        case SYS_READ:
            regs->eax = syscall_read((char *)regs->ebx, regs->ecx);
            break;
        case SYS_GETPID:
            regs->eax = task_get_current_id();
            break;
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
