#include "linux/vmalloc.h"

void *vmalloc(uint32_t size)
{
    (void)size;
    return 0;
}

void vfree(void *addr)
{
    (void)addr;
}

void *ioremap(uint32_t phys, uint32_t size)
{
    (void)size;
    return (void*)(uintptr_t)phys;
}

void iounmap(void *addr)
{
    (void)addr;
}

void *kmap(uint32_t pfn)
{
    return (void*)((uintptr_t)pfn << 12);
}

void kunmap(void *addr)
{
    (void)addr;
}
