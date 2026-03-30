#ifndef LINUX_MEMLAYOUT_H
#define LINUX_MEMLAYOUT_H

#include <stdint.h>
#include "linux/bootmem.h"
#include "linux/panic.h"
#include "asm/page.h"
#include "asm/pgtable.h"

static inline uint32_t memlayout_align_up_u32(uint32_t v, uint32_t align)
{
    if (align == 0)
        return v;
    uint32_t mask = align - 1;
    return (v + mask) & ~mask;
}

static inline uint32_t memlayout_directmap_start(void)
{
    return PAGE_OFFSET;
}

static inline uint32_t memlayout_lowmem_phys_end(void)
{
    return memlayout_align_up_u32(bootmem_lowmem_end(), PGDIR_SIZE);
}

static inline uint32_t memlayout_directmap_end(void)
{
    return PAGE_OFFSET + memlayout_lowmem_phys_end();
}

static inline uint32_t memlayout_vmalloc_start(void)
{
    uint32_t start = memlayout_directmap_end();
    if (start + VMALLOC_OFFSET >= start)
        start += VMALLOC_OFFSET;
    return memlayout_align_up_u32(start, PAGE_SIZE);
}

static inline uint32_t memlayout_vmalloc_end(void)
{
    return FIXADDR_START;
}

static inline uint32_t memlayout_fixaddr_start(void)
{
    return FIXADDR_START;
}

static inline void *memlayout_directmap_phys_to_virt(uint32_t phys)
{
    if (bootmem_lowmem_end() != 0 && phys >= memlayout_lowmem_phys_end())
        panic("directmap_phys_to_virt: phys out of range");
    return phys_to_virt(phys);
}

#endif
