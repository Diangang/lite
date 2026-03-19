#include "tss.h"
#include "gdt.h"
#include "libc.h"

typedef struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static tss_entry_t tss_entry;
static uint8_t tss_stack[4096] __attribute__((aligned(16)));

void tss_init(void)
{
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = base + sizeof(tss_entry);

    memset(&tss_entry, 0, sizeof(tss_entry));
    gdt_set_gate(5, base, limit, 0x89, 0x00);

    tss_set_kernel_stack((uint32_t)tss_stack + sizeof(tss_stack));

    tss_entry.ss0 = 0x10;
    tss_entry.cs = 0x0B;
    tss_entry.ss = 0x13;
    tss_entry.ds = 0x13;
    tss_entry.es = 0x13;
    tss_entry.fs = 0x13;
    tss_entry.gs = 0x13;
    tss_entry.iomap_base = sizeof(tss_entry);
}

void tss_set_kernel_stack(uint32_t stack)
{
    tss_entry.esp0 = stack;
}
