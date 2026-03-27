#include "linux/libc.h"
#include "linux/pmm.h"
#include "linux/vmm.h"
#include "linux/kheap.h"

void init_mm(struct multiboot_info* mbi)
{
    if (mbi->mods_addr == 0)
        panic("No Multiboot modules found! InitRD not loaded.");

    /* Initialize Physical Memory Manager */
    init_pmm(mbi);
    printf("PMM initialized.\n");

    /* Initialize Virtual Memory Manager */
    init_vmm();
    printf("VMM initialized.\n");

    /* Initialize Kernel Heap */
    init_kheap();
    printf("KHEAP initialized.\n");
}
