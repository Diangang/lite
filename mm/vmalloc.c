#include "linux/vmalloc.h"
#include "linux/page_alloc.h"
#include "linux/slab.h"
#include "linux/memlayout.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "linux/libc.h"

struct vmalloc_block {
    uint32_t vaddr;
    uint32_t size;
    struct vmalloc_block *next;
};

static struct vmalloc_block *vmalloc_list = NULL;
static uint32_t vmalloc_base;
static uint32_t vmalloc_end;

static void vmalloc_init_range(void)
{
    if (vmalloc_end)
        return;
    vmalloc_base = memlayout_vmalloc_start();
    vmalloc_end = memlayout_vmalloc_end();
}

void *vmalloc(uint32_t size)
{
    if (size == 0)
        return 0;
    vmalloc_init_range();
    uint32_t aligned = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (vmalloc_base + aligned < vmalloc_base)
        return 0;
    if (vmalloc_base + aligned > vmalloc_end)
        return 0;
    uint32_t vaddr = vmalloc_base;
    uint32_t pages = aligned / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        void *phys = alloc_page(GFP_KERNEL);
        if (!phys) {
            for (uint32_t j = 0; j < i; j++) {
                uint32_t p = virt_to_phys((void*)(vaddr + j * PAGE_SIZE));
                if (p != 0xFFFFFFFF)
                    free_page(p);
            }
            return 0;
        }
        map_page_ex(get_pgd_kernel(), phys, (void*)(vaddr + i * PAGE_SIZE), PTE_PRESENT | PTE_READ_WRITE);
    }
    vmalloc_base += aligned;
    struct vmalloc_block *blk = (struct vmalloc_block*)kmalloc(sizeof(*blk));
    if (!blk)
        return (void*)vaddr;
    blk->vaddr = vaddr;
    blk->size = aligned;
    blk->next = vmalloc_list;
    vmalloc_list = blk;
    return (void*)vaddr;
}

void vfree(void *addr)
{
    if (!addr)
        return;
    uint32_t vaddr = (uint32_t)addr;
    struct vmalloc_block *prev = NULL;
    struct vmalloc_block *cur = vmalloc_list;
    while (cur) {
        if (cur->vaddr == vaddr) {
            uint32_t pages = cur->size / PAGE_SIZE;
            for (uint32_t i = 0; i < pages; i++) {
                uint32_t phys = virt_to_phys((void*)(vaddr + i * PAGE_SIZE));
                if (phys != 0xFFFFFFFF)
                    free_page(phys);
            }
            if (prev)
                prev->next = cur->next;
            else
                vmalloc_list = cur->next;
            kfree(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

void *ioremap(uint32_t phys, uint32_t size)
{
    if (size == 0)
        return 0;
    vmalloc_init_range();
    uint32_t aligned = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (vmalloc_base + aligned < vmalloc_base)
        return 0;
    if (vmalloc_base + aligned > vmalloc_end)
        return 0;
    uint32_t vaddr = vmalloc_base;
    uint32_t pages = aligned / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        void *paddr = (void*)(phys + i * PAGE_SIZE);
        map_page_ex(get_pgd_kernel(), paddr, (void*)(vaddr + i * PAGE_SIZE),
                    PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE);
    }
    vmalloc_base += aligned;
    return (void*)vaddr;
}

void iounmap(void *addr)
{
    (void)addr;
}

void *kmap(uint32_t pfn)
{
    uint32_t phys = pfn << PAGE_SHIFT;
    if (phys < memlayout_lowmem_phys_end())
        return memlayout_directmap_phys_to_virt(phys);
    map_page_ex(get_pgd_kernel(), (void*)phys, (void*)memlayout_fixaddr_start(), PTE_PRESENT | PTE_READ_WRITE);
    return (void*)memlayout_fixaddr_start();
}

void kunmap(void *addr)
{
    (void)addr;
}
