#ifndef LINUX_MM_H
#define LINUX_MM_H

#include <stdint.h>
#include "asm/pgtable.h"

struct vm_area_struct {
    uint32_t vm_start;
    uint32_t vm_end;
    uint32_t vm_flags;
    uint32_t anon_vma_id;
    struct vm_area_struct *vm_next;
};

struct mm_struct {
    pgd_t *pgd;
    uint32_t start_code;
    uint32_t end_code;
    uint32_t start_stack;
    uint32_t start_brk;
    uint32_t brk;
    struct vm_area_struct *mmap;
};

enum {
    VMA_READ = 1 << 0,
    VMA_WRITE = 1 << 1,
    VMA_EXEC = 1 << 2
};

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);
struct mm_struct *dup_mm(struct mm_struct *src);

void mm_reset_mmap(struct mm_struct *mm);
void mm_add_vma(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags);

uint32_t do_mmap(struct mm_struct *mm, uint32_t addr, uint32_t length, uint32_t prot);
int do_munmap(struct mm_struct *mm, uint32_t addr, uint32_t length);
uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot);
int sys_munmap(uint32_t addr, uint32_t length);
uint32_t sys_mprotect(uint32_t addr, uint32_t length, uint32_t prot);
uint32_t sys_mremap(uint32_t addr, uint32_t old_length, uint32_t new_length);

void mm_init_brk(struct mm_struct *mm, uint32_t heap_base, uint32_t stack_base);
uint32_t do_brk(struct mm_struct *mm, uint32_t new_end);
uint32_t sys_brk(uint32_t new_end);

int vma_allows(struct mm_struct *mm, uint32_t addr, int is_write, int is_exec);

#endif
