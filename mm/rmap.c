#include "linux/rmap.h"
#include "linux/mmzone.h"
#include "linux/slab.h"

struct rmap_item {
    struct mm_struct *mm;
    uint32_t vaddr;
    struct rmap_item *next;
};

/* rmap_add: Implement rmap add. */
void rmap_add(struct mm_struct *mm, uint32_t vaddr, uint32_t phys)
{
    (void)mm;
    uint32_t pfn = phys / 4096;
    struct page *pg = pfn_to_page(pfn);
    if (!pg)
        return;
    if (pg->mapcount == 0) {
        pg->map_mm = mm;
        pg->map_vaddr = vaddr;
        pg->mapcount = 1;
        lru_add_inactive(pg);
        return;
    }
    if (pg->map_mm == mm && pg->map_vaddr == vaddr)
        return;
    struct rmap_item *it = pg->rmap_list;
    while (it) {
        if (it->mm == mm && it->vaddr == vaddr)
            return;
        it = it->next;
    }
    struct rmap_item *node = (struct rmap_item*)kmalloc(sizeof(struct rmap_item));
    if (!node)
        return;
    node->mm = mm;
    node->vaddr = vaddr;
    node->next = pg->rmap_list;
    pg->rmap_list = node;
    if (pg->mapcount < 0xFFFF)
        pg->mapcount++;
}

/* rmap_remove: Implement rmap remove. */
void rmap_remove(struct mm_struct *mm, uint32_t vaddr, uint32_t phys)
{
    (void)vaddr;
    uint32_t pfn = phys / 4096;
    struct page *pg = pfn_to_page(pfn);
    if (!pg)
        return;
    if (!pg->mapcount)
        return;
    if (pg->map_mm == mm && pg->map_vaddr == vaddr) {
        if (pg->mapcount == 1) {
            pg->map_mm = 0;
            pg->map_vaddr = 0;
            pg->mapcount = 0;
            lru_del(pg);
            return;
        }
        struct rmap_item *node = pg->rmap_list;
        if (node) {
            pg->rmap_list = node->next;
            pg->map_mm = node->mm;
            pg->map_vaddr = node->vaddr;
            kfree(node);
        } else {
            pg->map_mm = 0;
            pg->map_vaddr = 0;
        }
        if (pg->mapcount)
            pg->mapcount--;
        if (pg->mapcount == 0)
            lru_del(pg);
        return;
    }
    struct rmap_item *prev = 0;
    struct rmap_item *cur = pg->rmap_list;
    while (cur) {
        if (cur->mm == mm && cur->vaddr == vaddr) {
            if (prev)
                prev->next = cur->next;
            else
                pg->rmap_list = cur->next;
            kfree(cur);
            if (pg->mapcount)
                pg->mapcount--;
            if (pg->mapcount == 0)
                lru_del(pg);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/* rmap_dup: Implement rmap dup. */
void rmap_dup(struct mm_struct *src_mm, struct mm_struct *dst_mm, uint32_t vaddr, uint32_t phys)
{
    (void)src_mm;
    rmap_add(dst_mm, vaddr, phys);
}

/* page_mapcount: Implement page mapcount. */
uint16_t page_mapcount(unsigned long phys)
{
    uint32_t pfn = phys / 4096;
    struct page *pg = pfn_to_page(pfn);
    if (!pg)
        return 0;
    return pg->mapcount;
}
