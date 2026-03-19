#include "gdt.h"
#include "tss.h"

gdt_entry_t gdt_entries[6];
gdt_ptr_t   gdt_ptr;

/* Defined in gdt_flush.s */
extern void gdt_flush(uint32_t);

void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

void init_gdt(void)
{
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
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

    tss_init();

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush(0x28);
}
