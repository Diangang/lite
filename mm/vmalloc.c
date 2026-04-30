#include "linux/vmalloc.h"
#include "linux/gfp.h"
#include "linux/mm.h"
#include "linux/slab.h"
#include "asm/pgtable.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

struct vmalloc_block {
    uint32_t vaddr;
    uint32_t size;
    uint32_t phys_base;
    uint32_t page_off;
    uint32_t flags;
    struct vmalloc_block *next;
};

static struct vmalloc_block *vmalloc_list = NULL;
/* vmalloc_init_range: Implement vmalloc init range. */
static uint32_t vmalloc_base;
static uint32_t vmalloc_end;

#define VMALLOC_BLOCK_IOREMAP 0x1u

static void vmalloc_init_range(void)
{
    if (vmalloc_end)
        return;
    vmalloc_base = memlayout_vmalloc_start();
    vmalloc_end = memlayout_vmalloc_end();
}

static struct vmalloc_block *vmalloc_find_block(uint32_t vaddr, struct vmalloc_block **out_prev)
{
    struct vmalloc_block *prev = NULL;
    struct vmalloc_block *cur = vmalloc_list;
    while (cur) {
        if (cur->vaddr == vaddr) {
            if (out_prev)
                *out_prev = prev;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

static uint32_t vmalloc_find_gap(uint32_t size)
{
    uint32_t addr = memlayout_vmalloc_start();
    struct vmalloc_block *best = vmalloc_list;

    while (best) {
        if (best->vaddr >= addr && best->vaddr - addr >= size)
            return addr;
        if (best->vaddr + best->size > addr)
            addr = best->vaddr + best->size;
        best = best->next;
    }

    if (addr + size < addr || addr + size > vmalloc_end)
        return 0;
    return addr;
}

static void vmalloc_list_insert(struct vmalloc_block *blk)
{
    if (!blk)
        return;
    if (!vmalloc_list || blk->vaddr < vmalloc_list->vaddr) {
        blk->next = vmalloc_list;
        vmalloc_list = blk;
        return;
    }
    struct vmalloc_block *cur = vmalloc_list;
    while (cur->next && cur->next->vaddr < blk->vaddr)
        cur = cur->next;
    blk->next = cur->next;
    cur->next = blk;
}

static void vmalloc_unmap_block(struct vmalloc_block *blk)
{
    if (!blk)
        return;
    uint32_t pages = blk->size / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        uint32_t va = blk->vaddr + i * PAGE_SIZE;
        uint32_t phys = virt_to_phys((void *)va);
        unmap_page_pgd(get_pgd_kernel(), (void *)va);
        if (!(blk->flags & VMALLOC_BLOCK_IOREMAP) && phys != 0xFFFFFFFF)
            free_page(phys);
    }
}

static struct vmalloc_block *vmalloc_alloc_block(uint32_t size, uint32_t flags, uint32_t phys_base, uint32_t page_off)
{
    uint32_t vaddr = vmalloc_find_gap(size);
    if (!vaddr)
        return NULL;
    struct vmalloc_block *blk = (struct vmalloc_block *)kmalloc(sizeof(*blk));
    if (!blk)
        return NULL;
    blk->vaddr = vaddr;
    blk->size = size;
    blk->phys_base = phys_base;
    blk->page_off = page_off;
    blk->flags = flags;
    blk->next = NULL;
    vmalloc_list_insert(blk);
    return blk;
}

/* vmalloc: Implement vmalloc. */
void *vmalloc(unsigned long size)
{
    if (size == 0)
        return 0;
    vmalloc_init_range();
    uint32_t aligned = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    struct vmalloc_block *blk = vmalloc_alloc_block(aligned, 0, 0, 0);
    if (!blk)
        return 0;
    uint32_t vaddr = blk->vaddr;
    uint32_t pages = aligned / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        void *phys = alloc_page(GFP_KERNEL);
        if (!phys) {
            for (uint32_t j = 0; j < i; j++) {
                uint32_t p = virt_to_phys((void*)(vaddr + j * PAGE_SIZE));
                unmap_page_pgd(get_pgd_kernel(), (void *)(vaddr + j * PAGE_SIZE));
                if (p != 0xFFFFFFFF)
                    free_page(p);
            }
            struct vmalloc_block *prev = NULL;
            struct vmalloc_block *cur = vmalloc_find_block(vaddr, &prev);
            if (cur) {
                if (prev)
                    prev->next = cur->next;
                else
                    vmalloc_list = cur->next;
                kfree(cur);
            }
            return 0;
        }
        map_page_ex(get_pgd_kernel(), phys, (void*)(vaddr + i * PAGE_SIZE), PTE_PRESENT | PTE_READ_WRITE);
    }
    return (void*)vaddr;
}

void *vzalloc(unsigned long size)
{
    void *addr = vmalloc(size);
    if (addr)
        memset(addr, 0, size);
    return addr;
}

void *vmalloc_exec(unsigned long size)
{
    return vmalloc(size);
}

void *vmalloc_32(unsigned long size)
{
    return vmalloc(size);
}

int is_vmalloc_or_module_addr(const void *x)
{
    return is_vmalloc_addr(x);
}

/* vfree: Implement vfree. */
void vfree(const void *addr)
{
    if (!addr)
        return;
    uint32_t vaddr = (uint32_t)addr;
    struct vmalloc_block *prev = NULL;
    struct vmalloc_block *cur = vmalloc_find_block(vaddr, &prev);
    if (!cur || (cur->flags & VMALLOC_BLOCK_IOREMAP))
        return;
    vmalloc_unmap_block(cur);
    if (prev)
        prev->next = cur->next;
    else
        vmalloc_list = cur->next;
    kfree(cur);
}

void vmalloc_sync_all(void)
{
}

/* ioremap: Implement ioremap. */
void *ioremap(uint32_t phys, uint32_t size)
{
    if (size == 0)
        return 0;
    vmalloc_init_range();
    uint32_t page_off = phys & (PAGE_SIZE - 1);
    uint32_t phys_base = phys & ~(PAGE_SIZE - 1);
    uint32_t span = size + page_off;
    if (span < size)
        return 0;
    uint32_t aligned = (span + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    struct vmalloc_block *blk = vmalloc_alloc_block(aligned, VMALLOC_BLOCK_IOREMAP, phys_base, page_off);
    if (!blk)
        return 0;
    uint32_t vaddr = blk->vaddr;
    uint32_t pages = aligned / PAGE_SIZE;
    for (uint32_t i = 0; i < pages; i++) {
        void *paddr = (void*)(phys_base + i * PAGE_SIZE);
        map_page_ex(get_pgd_kernel(), paddr, (void*)(vaddr + i * PAGE_SIZE),
                    PTE_PRESENT | PTE_READ_WRITE | PTE_CACHE_DISABLE);
    }
    return (void*)(vaddr + page_off);
}

/* iounmap: Implement iounmap. */
void iounmap(void *addr)
{
    if (!addr)
        return;
    uint32_t raw = (uint32_t)addr;
    struct vmalloc_block *prev = NULL;
    struct vmalloc_block *cur = vmalloc_list;
    while (cur) {
        uint32_t start = cur->vaddr + cur->page_off;
        uint32_t end = cur->vaddr + cur->size;
        if ((cur->flags & VMALLOC_BLOCK_IOREMAP) && raw >= start && raw < end) {
            vmalloc_unmap_block(cur);
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

/* kmap: Implement kmap. */
void *kmap(uint32_t pfn)
{
    uint32_t phys = pfn << PAGE_SHIFT;
    if (phys < memlayout_lowmem_phys_end())
        return memlayout_directmap_phys_to_virt(phys);
    map_page_ex(get_pgd_kernel(), (void*)phys, (void*)memlayout_fixaddr_start(), PTE_PRESENT | PTE_READ_WRITE);
    return (void*)memlayout_fixaddr_start();
}

/* kunmap: Implement kunmap. */
void kunmap(void *addr)
{
    (void)addr;
}
