#ifndef LINUX_SWAP_H
#define LINUX_SWAP_H

#include <stdint.h>
#include "asm/pgtable.h"

struct page;
struct mm_struct;

/*
 * Linux mapping:
 * - swp_entry_t stored in non-present PTEs (pte_to_swp_entry/swp_entry_to_pte).
 * - swap map tracking used slots in a swap area.
 *
 * Lite keeps a single swap "type" and uses a small in-memory swap slot array.
 */
pteval_t swap_pte_encode(uint32_t slot);
int swap_pte_decode(pteval_t pte, uint32_t *slot);
void swap_free_slot(uint32_t slot);

void swap_init(void);
int swap_out_page(struct page *page);
int swap_in_mm(struct mm_struct *mm, uint32_t vaddr);

#endif
