#include "linux/panic.h"
#include "linux/printk.h"

/* panic: Panic on the subsystem. */
void panic(const char *msg)
{
    if (msg)
        printk("HALT: %s\n", msg);

    __asm__ volatile ("cli");

    for (;;)
        __asm__ volatile ("hlt");
}
