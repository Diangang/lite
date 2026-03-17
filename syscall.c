#include "syscall.h"
#include "kernel.h"
#include "task.h"

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
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
