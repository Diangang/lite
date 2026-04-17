#ifndef ASM_PGTABLE_H
#define ASM_PGTABLE_H

#include <stdint.h>
#include "asm/ptrace.h"
#include "asm/page.h"

typedef uint32_t pteval_t;
typedef uint32_t pgdval_t;
typedef uint32_t pte_t;
typedef uint32_t pgd_t;

#define PAGE_SHIFT 12
#define PGDIR_SHIFT 22
#define PTRS_PER_PTE 1024
#define PTRS_PER_PGD 1024
#define PGDIR_SIZE (1UL << PGDIR_SHIFT)
#define PGDIR_MASK (~(PGDIR_SIZE - 1))

#define PTE_PRESENT       0x01
#define PTE_READ_WRITE    0x02
#define PTE_USER          0x04
#define PTE_WRITE_THROUGH 0x08
#define PTE_CACHE_DISABLE 0x10
#define PTE_ACCESSED      0x20
#define PTE_DIRTY         0x40
#define PTE_PAGE_SIZE     0x80
#define PTE_GLOBAL        0x100
#define PTE_COW           0x200

#define _PAGE_PRESENT       PTE_PRESENT
#define _PAGE_RW            PTE_READ_WRITE
#define _PAGE_USER          PTE_USER
#define _PAGE_PWT           PTE_WRITE_THROUGH
#define _PAGE_PCD           PTE_CACHE_DISABLE
#define _PAGE_ACCESSED      PTE_ACCESSED
#define _PAGE_DIRTY         PTE_DIRTY
#define _PAGE_PSE           PTE_PAGE_SIZE
#define _PAGE_GLOBAL        PTE_GLOBAL

static inline uint32_t pgd_index(uint32_t address)
{
    return address >> PGDIR_SHIFT;
}

static inline uint32_t pte_index(uint32_t address)
{
    return (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);
}

static inline int pgd_present(pgdval_t pgd)
{
    return pgd & _PAGE_PRESENT;
}

static inline int pte_present(pteval_t pte)
{
    return pte & _PAGE_PRESENT;
}

static inline int pte_user(pteval_t pte)
{
    return pte & _PAGE_USER;
}

static inline int pte_write(pteval_t pte)
{
    return pte & _PAGE_RW;
}

static inline uint32_t pte_pfn(pteval_t pte)
{
    return pte & ~(PAGE_SIZE - 1);
}

void paging_init(void);
void map_page(void* phys_addr, void* virt_addr);
void map_page_ex(pgd_t* pgd, void* phys_addr, void* virt_addr, pteval_t flags);
int page_mapped(void* virt_addr);
uint32_t virt_to_phys(void* virt_addr);
uint32_t virt_to_phys_pgd(pgd_t* pgd, void* virt_addr);
pteval_t get_pte_flags(pgd_t* pgd, void* virt_addr);
void set_pte_flags(pgd_t* pgd, void* virt_addr, pteval_t flags);
void unmap_page_pgd(pgd_t* pgd, void* virt_addr);
/* Raw PTE accessors (Linux mapping: pte_val()/set_pte_at()). */
pteval_t get_pte_raw(pgd_t* pgd, void* virt_addr);
void set_pte_raw(pgd_t* pgd, void* virt_addr, pteval_t pte);
void set_page_user(void* virt_addr);
void set_page_user_pgd(pgd_t* pgd, void* virt_addr);
void set_page_readonly_pgd(pgd_t* pgd, void* virt_addr);
pgd_t* pgd_clone_kernel(void);
void switch_pgd(pgd_t* pgd);
pgd_t* get_pgd_current(void);
pgd_t* get_pgd_kernel(void);
int access_ok(pgd_t* pgd, void* addr, uint32_t len, int write);
int __copy_from_user(void* dst, const void* src_user, uint32_t len);
int __copy_to_user(void* dst_user, const void* src, uint32_t len);
void get_cow_stats(uint32_t *faults, uint32_t *copies);
void get_pf_stats(uint32_t *total, uint32_t *present, uint32_t *not_present, uint32_t *write, uint32_t *user, uint32_t *kernel, uint32_t *reserved, uint32_t *prot, uint32_t *null, uint32_t *kernel_addr, uint32_t *out_of_range);
struct pt_regs *do_page_fault(struct pt_regs *regs);

#endif
