#include "linux/libc.h"

/* GDT Entry Structure */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* GDT Pointer Structure (passed to lgdt) */
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

static struct gdt_ptr          gdt_ptr;
static struct gdt_entry        gdt_entries[6];
static struct tss_entry        tss;
static uint8_t tss_stack[4096] __attribute__((aligned(16)));

/* gdt_flush: Implement GDT flush. */
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
        : "ax", "memory"
    );
}

/* gdt_set_gate: Implement GDT set gate. */
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

/* tss_set_kernel_stack: Implement TSS set kernel stack. */
void tss_set_kernel_stack(uint32_t stack)
{
    tss.esp0 = stack;
}

/* init_tss: Initialize TSS. */
static void init_tss(void)
{
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = base + sizeof(tss);

    memset(&tss, 0, sizeof(tss));
    gdt_set_gate(5, base, limit, 0x89, 0x00);

    tss_set_kernel_stack((uint32_t)tss_stack + sizeof(tss_stack));

    tss.ss0 = 0x10;
    tss.cs = 0x0B;
    tss.ss = 0x13;
    tss.ds = 0x13;
    tss.es = 0x13;
    tss.fs = 0x13;
    tss.gs = 0x13;
    tss.iomap_base = sizeof(tss);
}

/* init_gdt: Initialize GDT. */
void init_gdt(void)
{
    gdt_ptr.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    /* 0: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 1: Kernel Code Segment (Base=0, Limit=4GB, Type=Code, Ring=0) */
    /* Access: 0x9A = 1001 1010b (Present, Ring 0, Code/Data, Executable, Readable, Accessed) */
    /* Granularity: 0xCF = 1100 1111b (4KB Pages, 32-bit Mode) */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 2: Kernel Data Segment (Base=0, Limit=4GB, Type=Data, Ring=0) */
    /* Access: 0x92 = 1001 0010b (Present, Ring 0, Code/Data, Data, Writable, Accessed) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* 3: User Code Segment (Base=0, Limit=4GB, Type=Code, Ring=3) */
    /* Access: 0xFA = 1111 1010b (Present, Ring 3, Code/Data, Executable, Readable) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* 4: User Data Segment (Base=0, Limit=4GB, Type=Data, Ring=3) */
    /* Access: 0xF2 = 1111 0010b (Present, Ring 3, Code/Data, Data, Writable) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    init_tss();

    gdt_flush((uint32_t)&gdt_ptr, 0x28);
    printf("GDT initialized.\n");
}
