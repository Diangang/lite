#include "asm/desc.h"

#include <stdint.h>

#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/string.h"

/*
 * Linux mapping: descriptor tables and TSS are managed under arch/x86/kernel/cpu/
 * (see linux2.6/arch/x86/kernel/cpu/common.c + arch/x86/include/asm/desc.h).
 *
 * Lite keeps a minimal, single-CPU GDT + TSS implementation (subsetting Linux),
 * but the file placement follows Linux.
 */

/* GDT entry structure. */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

/* GDTR structure (passed to lgdt). */
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
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
} __attribute__((packed));

static struct gdt_ptr gdt_descr;
static struct gdt_entry gdt_table[6];
static struct tss_entry tss;
static uint8_t tss_stack[4096] __attribute__((aligned(16)));

static inline void gdt_flush(uint32_t gdt_ptr_addr, uint16_t tss_selector)
{
    __asm__ volatile(
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "ltr %w1\n"
        :
        : "r"(gdt_ptr_addr), "r"(tss_selector)
        : "ax", "memory");
}

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_table[num].base_low = (base & 0xFFFFu);
    gdt_table[num].base_middle = (uint8_t)((base >> 16) & 0xFFu);
    gdt_table[num].base_high = (uint8_t)((base >> 24) & 0xFFu);

    gdt_table[num].limit_low = (limit & 0xFFFFu);
    gdt_table[num].granularity = (uint8_t)((limit >> 16) & 0x0Fu);
    gdt_table[num].granularity |= (gran & 0xF0u);
    gdt_table[num].access = access;
}

void tss_set_kernel_stack(uint32_t stack)
{
    tss.esp0 = stack;
}

static void init_tss(void)
{
    uint32_t base = (uint32_t)(uintptr_t)&tss;
    uint32_t limit = base + (uint32_t)sizeof(tss);

    memset(&tss, 0, sizeof(tss));
    gdt_set_gate(5, base, limit, 0x89, 0x00);

    tss_set_kernel_stack((uint32_t)(uintptr_t)tss_stack + (uint32_t)sizeof(tss_stack));

    tss.ss0 = 0x10;
    tss.cs = 0x0B;
    tss.ss = 0x13;
    tss.ds = 0x13;
    tss.es = 0x13;
    tss.fs = 0x13;
    tss.gs = 0x13;
    tss.iomap_base = (uint16_t)sizeof(tss);
}

void init_gdt(void)
{
    gdt_descr.limit = (uint16_t)((sizeof(struct gdt_entry) * 6u) - 1u);
    gdt_descr.base = (uint32_t)(uintptr_t)&gdt_table;

    /* 0: Null descriptor. */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 1: Kernel code segment. */
    gdt_set_gate(1, 0, 0xFFFFFFFFu, 0x9A, 0xCF);

    /* 2: Kernel data segment. */
    gdt_set_gate(2, 0, 0xFFFFFFFFu, 0x92, 0xCF);

    /* 3: User code segment. */
    gdt_set_gate(3, 0, 0xFFFFFFFFu, 0xFA, 0xCF);

    /* 4: User data segment. */
    gdt_set_gate(4, 0, 0xFFFFFFFFu, 0xF2, 0xCF);

    init_tss();

    gdt_flush((uint32_t)(uintptr_t)&gdt_descr, 0x28);
    printf("GDT initialized.\n");
}
