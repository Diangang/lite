#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "isr.h"

/*
 * Page Directory Entry / Page Table Entry Attributes
 */
#define PTE_PRESENT       0x01
#define PTE_READ_WRITE    0x02
#define PTE_USER          0x04
#define PTE_WRITE_THROUGH 0x08
#define PTE_CACHE_DISABLE 0x10
#define PTE_ACCESSED      0x20
#define PTE_DIRTY         0x40
#define PTE_PAGE_SIZE     0x80 /* Only valid in PDE (4MB pages) */
#define PTE_GLOBAL        0x100

/*
 * Virtual Memory Manager Functions
 */
void vmm_init(void);
void vmm_map_page(void* phys_addr, void* virt_addr);
int vmm_is_mapped(void* virt_addr);
uint32_t vmm_virt_to_phys(void* virt_addr);
void page_fault_handler(registers_t *regs);

#endif
