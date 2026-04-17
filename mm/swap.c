#include "linux/swap.h"
#include "linux/mmzone.h"
#include "linux/mm.h"
#include "linux/sched.h"
#include "linux/rmap.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/memlayout.h"
#include "linux/page_alloc.h"
#include "asm/pgtable.h"
#include "asm/page.h"

#define SWAP_SLOTS 64

struct swap_slot {
    int used;
    uint8_t *data;
};

/* swap_init: Initialize swap. */
static struct swap_slot swap_slots[SWAP_SLOTS];

void swap_init(void)
{
    for (int i = 0; i < SWAP_SLOTS; i++) {
        swap_slots[i].used = 0;
        swap_slots[i].data = 0;
    }
}

pteval_t swap_pte_encode(uint32_t slot)
{
    /*
     * Linux mapping: swap entry encoded in a non-present PTE. Lite keeps only
     * a small slot index; slot+1 is stored to avoid encoding as 0.
     */
    return ((pteval_t)(slot + 1) << PAGE_SHIFT);
}

int swap_pte_decode(pteval_t pte, uint32_t *slot)
{
    if (!slot)
        return 0;
    if (pte_present(pte))
        return 0;
    uint32_t enc = (uint32_t)(pte >> PAGE_SHIFT);
    if (enc == 0)
        return 0;
    enc -= 1;
    if (enc >= SWAP_SLOTS)
        return 0;
    *slot = enc;
    return 1;
}

void swap_free_slot(uint32_t slot)
{
    if (slot >= SWAP_SLOTS)
        return;
    if (!swap_slots[slot].used)
        return;
    if (swap_slots[slot].data)
        kfree(swap_slots[slot].data);
    swap_slots[slot].data = 0;
    swap_slots[slot].used = 0;
}

/* swap_out_page: Implement swap out page. */
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
    memcpy(buf, memlayout_directmap_phys_to_virt(phys), PAGE_SIZE);

    set_pte_raw(page->map_mm->pgd, (void*)page->map_vaddr, swap_pte_encode((uint32_t)slot));
    rmap_remove(page->map_mm, page->map_vaddr, phys);
    free_page((unsigned long)phys);
    page->flags &= ~PG_ISOLATED;

    swap_slots[slot].used = 1;
    swap_slots[slot].data = buf;
    return 0;
}

/* swap_in_mm: Implement swap in memory manager. */
int swap_in_mm(struct mm_struct *mm, uint32_t vaddr)
{
    if (!mm)
        return 0;
    if (vaddr >= TASK_SIZE)
        return 0;

    pteval_t pte = get_pte_raw(mm->pgd, (void*)vaddr);
    uint32_t slot;
    if (!swap_pte_decode(pte, &slot))
        return 0;
    if (!swap_slots[slot].used || !swap_slots[slot].data)
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

    swap_free_slot(slot);
    return 1;
}
