#ifndef LINUX_VMALLOC_H
#define LINUX_VMALLOC_H

#include <stdint.h>

void *vmalloc(unsigned long size);
void *vzalloc(unsigned long size);
void vfree(const void *addr);
void *ioremap(uint32_t phys, uint32_t size);
void iounmap(void *addr);
void *kmap(uint32_t pfn);
void kunmap(void *addr);

#endif
