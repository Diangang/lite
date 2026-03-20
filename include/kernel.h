#ifndef KERNEL_H
#define KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libc.h"
#include "isr.h"

/* I/O Port Helper Functions */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void panic(const char *msg)
{
    if (msg) {
        printf("HALT: %s\n", msg);
        __asm__ volatile ("cli");
    }
    while (true)
        __asm__ volatile ("hlt");
}

void user_mode_launch(void);
int kernel_load_user_program(const char* name, uint32_t* entry, uint32_t* user_stack, uint32_t** out_dir,
                             uint32_t* out_base, uint32_t* out_pages, uint32_t* out_stack_base);

/* Memory Management (pmm.c & vmm.c) */
void pmm_print_info(void);

#endif
