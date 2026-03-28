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

#endif
