#ifndef LINUX_PMM_H
#define LINUX_PMM_H

#include <stdint.h>
#include "asm/multiboot.h"

#define PMM_PAGE_SIZE 4096

void init_pmm(struct multiboot_info* mbi);
void pmm_print_memory_map(void);
uint32_t pmm_get_total_kb(void);
uint32_t pmm_get_free_kb(void);

void* pmm_alloc_page(void);
void pmm_free_page(void* p);
void pmm_ref_page(void* p);
uint32_t pmm_get_refcount(void* p);

void pmm_print_info(void);

#endif
