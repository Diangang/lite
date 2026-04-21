#include <stdarg.h>

#include "linux/kernel.h"
#include "linux/printk.h"

/* panic: Halt the system after emitting a Linux-shaped panic message. */
void panic(const char *fmt, ...)
{
    if (fmt) {
        va_list args;

        printk("Kernel panic - not syncing: ");
        va_start(args, fmt);
        vprintk(fmt, args);
        va_end(args);
        printk("\n");
    }

    __asm__ volatile ("cli");

    for (;;)
        __asm__ volatile ("hlt");
}
