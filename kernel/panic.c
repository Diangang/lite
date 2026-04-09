#include "linux/libc.h"
#include "linux/panic.h"

/* panic: Panic on the subsystem. */
void panic(const char *msg)
{
    if (msg)
        printf("HALT: %s\n", msg);

    __asm__ volatile ("cli");

    for (;;)
        __asm__ volatile ("hlt");
}
