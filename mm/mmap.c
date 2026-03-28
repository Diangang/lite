#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "asm/pgtable.h"
#include "linux/page_alloc.h"
#include "asm/page.h"
#include "linux/irqflags.h"

static uint32_t align_up(uint32_t value)
{
    return (value + 0xFFF) & ~0xFFF;
}

static int task_free_user_page_mapped(pgd_t *dir, uint32_t va_page)
{
    if (!dir)
        return 0;
    uint32_t pde_idx = pgd_index(va_page);
    uint32_t pte_idx = pte_index(va_page);
    pgdval_t pde = dir[pde_idx];
    if (!pgd_present(pde))
        return 0;
    pte_t *table = (pte_t*)(pde & ~0xFFF);
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0;
    if (!pte_user(pte))
        return 0;

    uint32_t phys = pte_pfn(pte);
    table[pte_idx] = 0;
    free_page((unsigned long)phys);
    return 1;
}

static void vma_list_free(struct mm_struct *mm)
{
    if (!mm)
        return;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        struct vm_area_struct *next = v->vm_next;
        kfree(v);
        v = next;
    }
    mm->mmap = NULL;
}

static void vma_add(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags);
static int vma_range_free(struct mm_struct *mm, uint32_t start, uint32_t end);
static uint32_t vma_find_gap(struct mm_struct *mm, uint32_t size, uint32_t limit);
static struct vm_area_struct *vma_find_heap(struct mm_struct *mm);

struct mm_struct *mm_create(void)
{
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm)
        return NULL;
    memset(mm, 0, sizeof(*mm));
    mm->pgd = get_pgd_current();
    return mm;
}

void mm_destroy(struct mm_struct *mm)
{
    if (!mm)
        return;
    if (!mm->pgd || mm->pgd == get_pgd_kernel()) {
        vma_list_free(mm);
        kfree(mm);
        return;
    }

    struct vm_area_struct *v = mm->mmap;
    while (v) {
        uint32_t start = v->vm_start & ~0xFFF;
        uint32_t end = (v->vm_end + 0xFFF) & ~0xFFF;
        if (end > start) {
            for (uint32_t va = start; va < end; va += 4096)
                task_free_user_page_mapped(mm->pgd, va);
        }
        v = v->vm_next;
    }

    pgd_t *kernel_dir = get_pgd_kernel();
    for (uint32_t i = 0; i < 1024; i++) {
        pgdval_t pde = mm->pgd[i];
        if (!(pde & PTE_PRESENT))
            continue;

        uint32_t pde_phys = pde & ~0xFFF;
        uint32_t kernel_pde_phys = 0;
        if (kernel_dir && (kernel_dir[i] & PTE_PRESENT))
            kernel_pde_phys = kernel_dir[i] & ~0xFFF;
        if (kernel_pde_phys && kernel_pde_phys == pde_phys)
            continue;
        free_page((unsigned long)pde_phys);
    }

    free_page((unsigned long)mm->pgd);
    vma_list_free(mm);
    kfree(mm);
}

uint32_t do_mmap(struct mm_struct *mm, uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!mm)
        return 0;
    if (length == 0)
        return 0;
    uint32_t len = align_up(length);
    if (len == 0)
        return 0;
    uint32_t limit = mm->start_stack ? mm->start_stack : TASK_SIZE;
    if (limit <= 0x1000)
        return 0;

    if (addr != 0) {
        if (addr < 0x1000)
            return 0;
        if (addr & 0xFFF)
            return 0;
        if (addr + len > limit)
            return 0;
        if (!vma_range_free(mm, addr, addr + len))
            return 0;
    } else {
        addr = vma_find_gap(mm, len, limit);
        if (addr == 0)
            return 0;
    }

    uint32_t flags = 0;
    if (prot & VMA_READ) flags |= VMA_READ;
    if (prot & VMA_WRITE) flags |= VMA_WRITE;
    if (prot & VMA_EXEC) flags |= VMA_EXEC;
    vma_add(mm, addr, addr + len, flags);
    return addr;
}

uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!current || !current->mm)
        return 0;
    return do_mmap(current->mm, addr, length, prot);
}

int do_munmap(struct mm_struct *mm, uint32_t addr, uint32_t length)
{
    if (!mm)
        return -1;
    if (length == 0)
        return -1;
    if (addr < 0x1000)
        return -1;
    if (addr & 0xFFF)
        return -1;
    uint32_t len = align_up(length);
    if (len == 0)
        return -1;
    uint32_t end = addr + len;
    if (end <= addr)
        return -1;

    uint32_t flags = irq_save();
    struct vm_area_struct *v = mm->mmap;
    struct vm_area_struct *prev = NULL;
    while (v) {
        if (end <= v->vm_start || addr >= v->vm_end) {
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr <= v->vm_start && end >= v->vm_end) {
            struct vm_area_struct *next = v->vm_next;
            if (prev) prev->vm_next = next;
            else mm->mmap = next;
            kfree(v);
            v = next;
            continue;
        }
        if (addr <= v->vm_start && end < v->vm_end) {
            v->vm_start = end;
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr > v->vm_start && end >= v->vm_end) {
            v->vm_end = addr;
            prev = v;
            v = v->vm_next;
            continue;
        }
        if (addr > v->vm_start && end < v->vm_end) {
            struct vm_area_struct *right = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
            if (right) {
                right->vm_start = end;
                right->vm_end = v->vm_end;
                right->vm_flags = v->vm_flags;
                right->vm_next = v->vm_next;
                v->vm_end = addr;
                v->vm_next = right;
            } else {
                v->vm_end = addr;
            }
            prev = v;
            v = v->vm_next;
            continue;
        }
    }
    irq_restore(flags);

    uint32_t page_start = addr & ~0xFFF;
    uint32_t page_end = (end + 0xFFF) & ~0xFFF;
    for (uint32_t va = page_start; va < page_end; va += 4096)
        task_free_user_page_mapped(mm->pgd, va);
    return 0;
}

int sys_munmap(uint32_t addr, uint32_t length)
{
    if (!current || !current->mm)
        return -1;
    return do_munmap(current->mm, addr, length);
}

static void vma_add(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags)
{
    if (!mm)
        return;
    if (start >= end)
        return;
    struct vm_area_struct *v = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
    if (!v)
        return;
    v->vm_start = start;
    v->vm_end = end;
    v->vm_flags = flags;
    v->vm_next = mm->mmap;
    mm->mmap = v;
}

static int vma_range_free(struct mm_struct *mm, uint32_t start, uint32_t end)
{
    if (!mm)
        return 0;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (end <= v->vm_start || start >= v->vm_end) {
            v = v->vm_next;
            continue;
        }
        return 0;
    }
    return 1;
}

static uint32_t vma_find_gap(struct mm_struct *mm, uint32_t size, uint32_t limit)
{
    if (!mm)
        return 0;
    if (size == 0)
        return 0;
    uint32_t start = mm->brk ? align_up(mm->brk) : 0x400000;
    if (mm->start_code && mm->end_code) {
        uint32_t min = mm->end_code;
        if (start < min) start = align_up(min);
    }
    if (start < 0x1000) start = 0x1000;
    if (limit <= start)
        return 0;
    while (start + size <= limit) {
        if (vma_range_free(mm, start, start + size))
            return start;
        start += 0x1000;
    }
    return 0;
}

static struct vm_area_struct *vma_find_heap(struct mm_struct *mm)
{
    if (!mm)
        return NULL;
    if (!mm->start_brk)
        return NULL;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (v->vm_start == mm->start_brk && (v->vm_flags & (VMA_READ | VMA_WRITE)) == (VMA_READ | VMA_WRITE))
            return v;
        v = v->vm_next;
    }
    return NULL;
}

void mm_reset_mmap(struct mm_struct *mm)
{
    if (!mm)
        return;
    vma_list_free(mm);
}

void mm_add_vma(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags)
{
    if (!mm)
        return;
    vma_add(mm, start, end, flags);
}

int vma_allows(struct mm_struct *mm, uint32_t addr, int is_write, int is_exec)
{
    if (!mm)
        return 0;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (addr >= v->vm_start && addr < v->vm_end) {
            if (is_exec && !(v->vm_flags & VMA_EXEC))
                return 0;
            if (is_write && !(v->vm_flags & VMA_WRITE))
                return 0;
            if (!is_write && !(v->vm_flags & VMA_READ))
                return 0;
            return 1;
        }
        v = v->vm_next;
    }
    return 0;
}

void mm_init_brk(struct mm_struct *mm, uint32_t heap_base, uint32_t stack_base)
{
    if (!mm)
        return;
    if (heap_base < 0x1000)
        return;
    if (heap_base >= TASK_SIZE)
        return;
    if (stack_base && heap_base + 0x1000 >= stack_base)
        return;
    mm->start_brk = heap_base;
    mm->brk = heap_base;

    struct vm_area_struct *heap = vma_find_heap(mm);
    if (!heap)
        vma_add(mm, heap_base, heap_base, VMA_READ | VMA_WRITE);
}

uint32_t do_brk(struct mm_struct *mm, uint32_t new_end)
{
    if (!mm)
        return 0;
    if (mm->start_brk == 0)
        return 0;

    if (new_end == 0)
        return mm->brk;

    if (new_end < mm->start_brk)
        return mm->brk;
    if (new_end >= TASK_SIZE)
        return mm->brk;
    if (mm->start_stack && align_up(new_end) + 0x1000 > mm->start_stack)
        return mm->brk;

    mm->brk = new_end;

    struct vm_area_struct *heap = vma_find_heap(mm);
    if (heap) {
        uint32_t end = align_up(new_end);
        if (end < heap->vm_start) end = heap->vm_start;
        heap->vm_end = end;
    }

    return mm->brk;
}

uint32_t sys_brk(uint32_t new_end)
{
    if (!current || !current->mm)
        return 0;
    return do_brk(current->mm, new_end);
}
