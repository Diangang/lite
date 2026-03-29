#ifndef LINUX_SWAP_H
#define LINUX_SWAP_H

#include <stdint.h>

struct page;
struct mm_struct;

void swap_init(void);
int swap_out_page(struct page *page);
int swap_in_mm(struct mm_struct *mm, uint32_t vaddr);

#endif
