#include "linux/swap.h"
#include "linux/mmzone.h"
#include "linux/mm.h"
#include "linux/sched.h"
#include "linux/rmap.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/page_alloc.h"
#include "asm/pgtable.h"
#include "asm/page.h"

#define SWAP_SLOTS 64

struct swap_slot {
    int used;
    struct mm_struct *mm;
    uint32_t vaddr;
    uint8_t *data;
};

static struct swap_slot swap_slots[SWAP_SLOTS];

void swap_init(void)
{
    for (int i = 0; i < SWAP_SLOTS; i++) {
        swap_slots[i].used = 0;
        swap_slots[i].mm = 0;
        swap_slots[i].vaddr = 0;
        swap_slots[i].data = 0;
    }
}

int swap_out_page(struct page *page)
{
    if (!page)
        return -1;
    if (page->mapcount != 1)
        return -1;
    if (!page->map_mm)
        return -1;
    if (page->map_vaddr >= TASK_SIZE)
        return -1;
    if (page->rmap_list)
        return -1;
    if (!current || current->mm != page->map_mm)
        return -1;

    int slot = -1;
    for (int i = 0; i < SWAP_SLOTS; i++) {
        if (!swap_slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    pteval_t pte = get_pte_flags(page->map_mm->pgd, (void*)page->map_vaddr);
    if (!pte_present(pte))
        return -1;
    uint32_t phys = pte_pfn(pte);

    uint8_t *buf = (uint8_t*)kmalloc(PAGE_SIZE);
    if (!buf)
        return -1;
    memcpy(buf, (void*)page->map_vaddr, PAGE_SIZE);

    unmap_page_pgd(page->map_mm->pgd, (void*)page->map_vaddr);
    rmap_remove(page->map_mm, page->map_vaddr, phys);
    free_page((unsigned long)phys);

    swap_slots[slot].used = 1;
    swap_slots[slot].mm = page->map_mm;
    swap_slots[slot].vaddr = page->map_vaddr;
    swap_slots[slot].data = buf;
    return 0;
}

int swap_in_mm(struct mm_struct *mm, uint32_t vaddr)
{
    if (!mm)
        return 0;
    if (vaddr >= TASK_SIZE)
        return 0;

    int slot = -1;
    for (int i = 0; i < SWAP_SLOTS; i++) {
        if (swap_slots[i].used && swap_slots[i].mm == mm && swap_slots[i].vaddr == vaddr) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return 0;

    void *phys = alloc_page(GFP_KERNEL);
    if (!phys)
        return -1;

    pteval_t flags = PTE_PRESENT | PTE_USER;
    if (vma_allows(mm, vaddr, 1, 0))
        flags |= PTE_READ_WRITE;
    map_page_ex(mm->pgd, phys, (void*)vaddr, flags);
    memcpy((void*)vaddr, swap_slots[slot].data, PAGE_SIZE);
    rmap_add(mm, vaddr, (uint32_t)phys);

    kfree(swap_slots[slot].data);
    swap_slots[slot].used = 0;
    swap_slots[slot].mm = 0;
    swap_slots[slot].vaddr = 0;
    swap_slots[slot].data = 0;
    return 1;
}
