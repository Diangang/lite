#ifndef PMM_H
#define PMM_H

#include "multiboot.h"
#include <stdint.h>

/* Physical Page Size (4 KB) */
#define PMM_PAGE_SIZE 4096

void pmm_init(struct multiboot_info* mbi);
void pmm_print_memory_map(void);
uint32_t pmm_get_total_kb(void);
uint32_t pmm_get_free_kb(void);

/* Allocation API */
void* pmm_alloc_page(void);
void pmm_free_page(void* p);
void pmm_ref_page(void* p);
uint32_t pmm_get_refcount(void* p);

#endif
