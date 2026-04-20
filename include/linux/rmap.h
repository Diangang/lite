#ifndef LINUX_RMAP_H
#define LINUX_RMAP_H

#include <stdint.h>

struct mm_struct;

void page_add_anon_rmap(struct mm_struct *mm, uint32_t vaddr, uint32_t phys);
void page_remove_rmap(struct mm_struct *mm, uint32_t vaddr, uint32_t phys);
void page_dup_rmap(struct mm_struct *src_mm, struct mm_struct *dst_mm, uint32_t vaddr, uint32_t phys);
uint16_t page_mapcount(unsigned long phys);

#endif
