#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "asm/pgtable.h"
#include "linux/page_alloc.h"
#include "asm/page.h"
#include "linux/memlayout.h"
#include "linux/irqflags.h"
#include "linux/rmap.h"

/* align_up: Implement align up. */
static uint32_t align_up(uint32_t value)
{
    return (value + 0xFFF) & ~0xFFF;
}

/* task_free_user_page_mapped: Implement task free user page mapped. */
static int task_free_user_page_mapped(struct mm_struct *mm, pgd_t *dir, uint32_t va_page)
{
    if (!dir)
        return 0;
    uint32_t pde_idx = pgd_index(va_page);
    uint32_t pte_idx = pte_index(va_page);
    pgdval_t pde = dir[pde_idx];
    if (!pgd_present(pde))
        return 0;
    pte_t *table = (pte_t*)memlayout_directmap_phys_to_virt(pde & ~0xFFF);
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0;
    if (!pte_user(pte))
        return 0;

    uint32_t phys = pte_pfn(pte);
    table[pte_idx] = 0;
    if (mm)
        rmap_remove(mm, va_page, phys);
    free_page((unsigned long)phys);
    return 1;
}

/* vma_list_free: Implement VMA list free. */
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

/* vma_add: Implement VMA add. */
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

/* vma_range_free: Implement VMA range free. */
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

/* vma_find_covering: Implement VMA find covering. */
static struct vm_area_struct *vma_find_covering(struct mm_struct *mm, uint32_t start, uint32_t end)
{
    if (!mm)
        return NULL;
    struct vm_area_struct *v = mm->mmap;
    while (v) {
        if (start >= v->vm_start && end <= v->vm_end)
            return v;
        v = v->vm_next;
    }
    return NULL;
}

/* vma_find_gap: Implement VMA find gap. */
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

/* vma_find_heap: Implement VMA find heap. */
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

static struct vm_area_struct *vma_clone_list(struct vm_area_struct *src)
{
    struct vm_area_struct *head = NULL;
    struct vm_area_struct **tail = &head;
    while (src) {
        struct vm_area_struct *v = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
        if (!v) {
            while (head) {
                struct vm_area_struct *next = head->vm_next;
                kfree(head);
                head = next;
            }
            return NULL;
        }
        v->vm_start = src->vm_start;
        v->vm_end = src->vm_end;
        v->vm_flags = src->vm_flags;
        v->vm_next = NULL;
        *tail = v;
        tail = &v->vm_next;
        src = src->vm_next;
    }
    return head;
}

/* mm_create: Implement memory manager create. */
struct mm_struct *mm_create(void)
{
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm)
        return NULL;
    memset(mm, 0, sizeof(*mm));
    mm->pgd = get_pgd_current();
    return mm;
}

struct mm_struct *dup_mm(struct mm_struct *src)
{
    if (!src)
        return NULL;
    pgd_t *new_dir = pgd_clone_kernel();
    if (!new_dir)
        return NULL;
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm) {
        uint32_t pgd_vaddr = (uint32_t)new_dir;
        if (pgd_vaddr >= PAGE_OFFSET)
            free_page((unsigned long)virt_to_phys_addr(new_dir));
        else
            free_page((unsigned long)pgd_vaddr);
        return NULL;
    }
    memset(mm, 0, sizeof(*mm));
    mm->pgd = new_dir;
    mm->start_code = src->start_code;
    mm->end_code = src->end_code;
    mm->start_stack = src->start_stack;
    mm->start_brk = src->start_brk;
    mm->brk = src->brk;
    mm->mmap = vma_clone_list(src->mmap);
    if (src->mmap && !mm->mmap) {
        uint32_t pgd_vaddr = (uint32_t)new_dir;
        if (pgd_vaddr >= PAGE_OFFSET)
            free_page((unsigned long)virt_to_phys_addr(new_dir));
        else
            free_page((unsigned long)pgd_vaddr);
        kfree(mm);
        return NULL;
    }
    struct vm_area_struct *v = src->mmap;
    while (v) {
        uint32_t start = v->vm_start & ~0xFFFu;
        uint32_t end = (v->vm_end + 0xFFFu) & ~0xFFFu;
        for (uint32_t va = start; va < end; va += 4096) {
            pteval_t pte = get_pte_flags(src->pgd, (void*)va);
            if (!(pte & PTE_PRESENT) || !(pte & PTE_USER))
                continue;
            uint32_t phys = pte_pfn(pte);
            pteval_t flags = pte & 0xFFFu;
            if (flags & PTE_READ_WRITE) {
                flags &= ~PTE_READ_WRITE;
                flags |= PTE_COW;
                set_pte_flags(src->pgd, (void*)va, flags);
            }
            map_page_ex(new_dir, (void*)phys, (void*)va, flags);
            rmap_dup(src, mm, va, phys);
            get_page((unsigned long)phys);
        }
        v = v->vm_next;
    }
    return mm;
}

/* mm_destroy: Implement memory manager destroy. */
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
                task_free_user_page_mapped(mm, mm->pgd, va);
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

    uint32_t pgd_vaddr = (uint32_t)mm->pgd;
    if (pgd_vaddr >= PAGE_OFFSET)
        free_page((unsigned long)virt_to_phys_addr(mm->pgd));
    else
        free_page((unsigned long)pgd_vaddr);
    vma_list_free(mm);
    kfree(mm);
}

/* do_mmap: Perform mmap. */
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

/* sys_mmap: Implement sys mmap. */
uint32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!current || !current->mm)
        return 0;
    return do_mmap(current->mm, addr, length, prot);
}

/* do_munmap: Perform munmap. */
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
        task_free_user_page_mapped(mm, mm->pgd, va);
    return 0;
}

/* sys_munmap: Implement sys munmap. */
int sys_munmap(uint32_t addr, uint32_t length)
{
    if (!current || !current->mm)
        return -1;
    return do_munmap(current->mm, addr, length);
}

/* update_mapped_page_flags: Update mapped page flags. */
static int update_mapped_page_flags(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t prot)
{
    if (!mm || !mm->pgd)
        return -1;
    uint32_t flags = PTE_PRESENT | PTE_USER;
    if (prot & VMA_WRITE)
        flags |= PTE_READ_WRITE;
    uint32_t page_start = start & ~0xFFF;
    uint32_t page_end = (end + 0xFFF) & ~0xFFF;
    for (uint32_t va = page_start; va < page_end; va += 4096) {
        pteval_t pte = get_pte_flags(mm->pgd, (void*)va);
        if (!pte)
            continue;
        pteval_t new_flags = (pte & (PAGE_SIZE - 1));
        new_flags |= PTE_USER | PTE_PRESENT;
        if (flags & PTE_READ_WRITE)
            new_flags |= PTE_READ_WRITE;
        else
            new_flags &= ~PTE_READ_WRITE;
        if (!(flags & PTE_READ_WRITE))
            new_flags &= ~PTE_COW;
        set_pte_flags(mm->pgd, (void*)va, new_flags);
    }
    return 0;
}

/* do_mprotect: Perform mprotect. */
uint32_t do_mprotect(struct mm_struct *mm, uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!mm)
        return 0;
    if (length == 0)
        return 0;
    if (addr < 0x1000)
        return 0;
    if (addr & 0xFFF)
        return 0;
    uint32_t len = align_up(length);
    if (len == 0)
        return 0;
    uint32_t end = addr + len;
    if (end <= addr)
        return 0;

    uint32_t flags = 0;
    if (prot & VMA_READ) flags |= VMA_READ;
    if (prot & VMA_WRITE) flags |= VMA_WRITE;
    if (prot & VMA_EXEC) flags |= VMA_EXEC;

    struct vm_area_struct *v = vma_find_covering(mm, addr, end);
    if (!v)
        return 0;

    if (addr > v->vm_start)
        vma_add(mm, v->vm_start, addr, v->vm_flags);
    if (end < v->vm_end)
        vma_add(mm, end, v->vm_end, v->vm_flags);
    v->vm_start = addr;
    v->vm_end = end;
    v->vm_flags = flags;

    update_mapped_page_flags(mm, addr, end, flags);
    return 1;
}

/* sys_mprotect: Implement sys mprotect. */
uint32_t sys_mprotect(uint32_t addr, uint32_t length, uint32_t prot)
{
    if (!current || !current->mm)
        return 0;
    return do_mprotect(current->mm, addr, length, prot);
}

/* do_mremap: Perform mremap. */
uint32_t do_mremap(struct mm_struct *mm, uint32_t addr, uint32_t old_length, uint32_t new_length)
{
    if (!mm)
        return 0;
    if (old_length == 0 || new_length == 0)
        return 0;
    if (addr < 0x1000)
        return 0;
    if (addr & 0xFFF)
        return 0;
    uint32_t old_len = align_up(old_length);
    uint32_t new_len = align_up(new_length);
    if (old_len == 0 || new_len == 0)
        return 0;
    uint32_t old_end = addr + old_len;
    if (old_end <= addr)
        return 0;
    uint32_t new_end = addr + new_len;
    if (new_end <= addr)
        return 0;

    struct vm_area_struct *v = vma_find_covering(mm, addr, old_end);
    if (!v)
        return 0;

    if (new_len == old_len)
        return addr;

    if (new_len < old_len) {
        v->vm_end = addr + new_len;
        uint32_t page_start = v->vm_end & ~0xFFF;
        uint32_t page_end = (old_end + 0xFFF) & ~0xFFF;
        for (uint32_t va = page_start; va < page_end; va += 4096)
            task_free_user_page_mapped(mm, mm->pgd, va);
        return addr;
    }

    uint32_t limit = mm->start_stack ? mm->start_stack : TASK_SIZE;
    if (new_end > limit)
        return 0;
    if (!vma_range_free(mm, old_end, new_end))
        return 0;
    v->vm_end = new_end;
    return addr;
}

/* sys_mremap: Implement sys mremap. */
uint32_t sys_mremap(uint32_t addr, uint32_t old_length, uint32_t new_length)
{
    if (!current || !current->mm)
        return 0;
    return do_mremap(current->mm, addr, old_length, new_length);
}

/* mm_reset_mmap: Implement memory manager reset mmap. */
void mm_reset_mmap(struct mm_struct *mm)
{
    if (!mm)
        return;
    vma_list_free(mm);
}

/* mm_add_vma: Implement memory manager add VMA. */
void mm_add_vma(struct mm_struct *mm, uint32_t start, uint32_t end, uint32_t flags)
{
    if (!mm)
        return;
    vma_add(mm, start, end, flags);
}

/* vma_allows: Implement VMA allows. */
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

/* mm_init_brk: Implement memory manager init brk. */
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

/* do_brk: Perform brk. */
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

/* sys_brk: Implement sys brk. */
uint32_t sys_brk(uint32_t new_end)
{
    if (!current || !current->mm)
        return 0;
    return do_brk(current->mm, new_end);
}
