#ifndef ASM_PAGE_H
#define ASM_PAGE_H

#include <stdint.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)

#define PAGE_OFFSET 0xC0000000
#define TASK_SIZE PAGE_OFFSET

#define VMALLOC_OFFSET (8 * 1024 * 1024)
#define FIXADDR_START 0xFF000000

#define USER_STACK_TOP TASK_SIZE
#define USER_STACK_PAGES 8
#define USER_STACK_BASE (USER_STACK_TOP - (USER_STACK_PAGES * PAGE_SIZE))

static inline void *phys_to_virt(uint32_t phys)
{
    return (void*)(phys + PAGE_OFFSET);
}

static inline uint32_t virt_to_phys_addr(const void *virt)
{
    return (uint32_t)virt - PAGE_OFFSET;
}

#endif
