#include "linux/libc.h"
#include "linux/bootmem.h"
#include "linux/mmzone.h"
#include "linux/page_alloc.h"
#include "linux/vmscan.h"
#include "asm/pgtable.h"
#include "linux/slab.h"

void init_mm(struct multiboot_info* mbi)
{
    if (mbi->mods_addr == 0)
        panic("No Multiboot modules found! InitRD not loaded.");

    bootmem_init(mbi);
    printf("BOOTMEM initialized.\n");

    init_zones();
    printf("ZONES initialized.\n");

    build_all_zonelists();
    printf("ZONELISTS initialized.\n");

    free_area_init(mbi);
    printf("PAGE_ALLOC initialized.\n");

    paging_init();
    printf("PAGING initialized.\n");

    mem_init();
    printf("MEM initialized.\n");

    kswapd_init();
    printf("VMSCAN initialized.\n");

    kmem_cache_init();
    printf("SLAB initialized.\n");
}
