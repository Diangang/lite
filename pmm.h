#ifndef PMM_H
#define PMM_H

#include "multiboot.h"
#include <stdint.h>

/* Physical Page Size (4 KB) */
#define PMM_PAGE_SIZE 4096

void pmm_init(multiboot_info_t* mbi);
void pmm_print_memory_map(void);

/* Allocation API */
void* pmm_alloc_page(void);
void pmm_free_page(void* p);

#endif