#ifndef LINUX_BOOTMEM_H
#define LINUX_BOOTMEM_H

#include <stdint.h>

struct multiboot_info;

void bootmem_init(struct multiboot_info *mbi);
void *bootmem_alloc(uint32_t size, uint32_t align);
void bootmem_reserve(uint32_t base, uint32_t size);
int bootmem_is_reserved(uint32_t base, uint32_t size);
uint32_t bootmem_start(void);
uint32_t bootmem_end(void);
uint32_t bootmem_total_pages(void);
uint32_t bootmem_present_pages(uint32_t start_pfn, uint32_t end_pfn);
uint32_t bootmem_lowmem_end(void);
uint32_t bootmem_ram_kb(void);
uint32_t bootmem_reserved_kb(void);

uint32_t bootmem_e820_entries(void);
int bootmem_e820_get(uint32_t idx, uint32_t *base, uint32_t *size, uint32_t *type);
uint32_t bootmem_kernel_phys_start(void);
uint32_t bootmem_kernel_phys_end(void);
uint32_t bootmem_module_count(void);
int bootmem_module_get(uint32_t idx, uint32_t *start, uint32_t *end);

#endif
