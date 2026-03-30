#include "asm/pgtable.h"
#include "asm/page.h"
#include "linux/page_alloc.h"
#include "linux/libc.h"
#include "linux/interrupt.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "linux/exit.h"
#include "linux/slab.h"
#include "linux/rmap.h"
#include "linux/swap.h"
#include "linux/bootmem.h"

static pgd_t* page_directory = NULL;
static pgd_t* kernel_directory = NULL;
static uint32_t cow_faults = 0;
static uint32_t cow_copies = 0;

extern void load_page_directory(uint32_t*);
extern void enable_paging(void);

void map_page_ex(pgd_t* pgd, void* phys_addr, void* virt_addr, pteval_t flags)
{
    if (!pgd)
        return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    if (!pgd_present(pgd[pde_idx])) {
        uint32_t new_table_phys = (uint32_t)alloc_page(GFP_KERNEL);
        if (!new_table_phys)
            return (void)printf("VMM: Failed to allocate Page Table for 0x%x\n", virt_addr);

        pte_t* new_table = (pte_t*)phys_to_virt(new_table_phys);
        memset(new_table, 0, PAGE_SIZE);
        pgd[pde_idx] = new_table_phys | PTE_READ_WRITE | PTE_PRESENT;
        if (flags & PTE_USER)
            pgd[pde_idx] |= PTE_USER;

        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
    } else if (flags & PTE_USER) {
        pgd[pde_idx] |= PTE_USER;
    }

    pte_t* table = (pte_t*)phys_to_virt(pgd[pde_idx] & ~(PAGE_SIZE - 1));
    table[pte_idx] = ((uint32_t)phys_addr) | flags;

    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void map_page(void* phys_addr, void* virt_addr)
{
    map_page_ex(page_directory, phys_addr, virt_addr, PTE_READ_WRITE | PTE_PRESENT);
}

int page_mapped(void* virt_addr)
{
    if (!page_directory)
        return 0;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = page_directory[pde_idx];
    if (!pgd_present(pde))
        return 0;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];

    return pte_present(pte) ? 1 : 0;
}

uint32_t virt_to_phys(void* virt_addr)
{
    if (!page_directory)
        return 0xFFFFFFFF;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = page_directory[pde_idx];
    if (!pgd_present(pde))
        return 0xFFFFFFFF;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0xFFFFFFFF;

    return pte_pfn(pte) + (va & (PAGE_SIZE - 1));
}

uint32_t virt_to_phys_pgd(pgd_t* pgd, void* virt_addr)
{
    if (!pgd)
        return 0xFFFFFFFF;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return 0xFFFFFFFF;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0xFFFFFFFF;

    return pte_pfn(pte) + (va & (PAGE_SIZE - 1));
}

pteval_t get_pte_flags(pgd_t* pgd, void* virt_addr)
{
    if (!pgd)
        return 0;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return 0;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0;

    return pte;
}

void set_pte_flags(pgd_t* pgd, void* virt_addr, pteval_t flags)
{
    if (!pgd)
        return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return;

    if (flags & PTE_USER)
        pgd[pde_idx] |= PTE_USER;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return;

    table[pte_idx] = (pte & ~(PAGE_SIZE - 1)) | (flags & (PAGE_SIZE - 1));
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void unmap_page_pgd(pgd_t* pgd, void* virt_addr)
{
    if (!pgd)
        return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return;

    table[pte_idx] = 0;
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void set_page_user(void* virt_addr)
{
    set_page_user_pgd(page_directory, virt_addr);
}

void set_page_user_pgd(pgd_t* pgd, void* virt_addr)
{
    if (!pgd)
        return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return;

    pgd[pde_idx] |= PTE_USER;
    table[pte_idx] = pte | PTE_USER;

    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void set_page_readonly_pgd(pgd_t* pgd, void* virt_addr)
{
    if (!pgd)
        return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);

    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return;

    table[pte_idx] = pte & ~PTE_READ_WRITE;
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

pgd_t* pgd_clone_kernel(void)
{
    if (!kernel_directory)
        return NULL;
    uint32_t new_dir_phys = (uint32_t)alloc_page(GFP_KERNEL);
    if (!new_dir_phys)
        return NULL;
    pgd_t* new_dir = (pgd_t*)phys_to_virt(new_dir_phys);
    memset(new_dir, 0, PAGE_SIZE);

    for (int i = 0; i < 1024; i++)
        new_dir[i] = kernel_directory[i];

    return new_dir;
}

void switch_pgd(pgd_t* pgd)
{
    if (!pgd)
        return;
    page_directory = pgd;
    uint32_t pgd_phys = virt_to_phys_addr(pgd);
    __asm__ volatile("mov %0, %%cr3" :: "r"(pgd_phys));
}

pgd_t* get_pgd_current(void)
{
    return page_directory;
}

pgd_t* get_pgd_kernel(void)
{
    return kernel_directory;
}

static int get_pte(pgd_t* pgd, uint32_t va, pgdval_t* out_pde, pteval_t* out_pte)
{
    if (!pgd)
        return 0;

    uint32_t pde_idx = pgd_index(va);
    uint32_t pte_idx = pte_index(va);
    pgdval_t pde = pgd[pde_idx];
    if (!pgd_present(pde))
        return 0;

    pte_t* table = (pte_t*)phys_to_virt(pde & ~(PAGE_SIZE - 1));
    pteval_t pte = table[pte_idx];
    if (!pte_present(pte))
        return 0;

    if (out_pde) *out_pde = pde;
    if (out_pte) *out_pte = pte;
    return 1;
}

int access_ok(pgd_t* pgd, void* addr, uint32_t len, int write)
{
    if (!pgd)
        return 0;
    if (len == 0)
        return 1;

    uint32_t start = (uint32_t)addr;
    uint32_t end = start + len - 1;

    if (start < PAGE_SIZE)
        return 0;
    if (end < start)
        return 0;
    if (end >= TASK_SIZE)
        return 0;

    uint32_t page = start & ~(PAGE_SIZE - 1);
    uint32_t last = end & ~(PAGE_SIZE - 1);
    while (1) {
        pgdval_t pde;
        pteval_t pte;
        if (!get_pte(pgd, page, &pde, &pte)) {
            if (!current || !current->mm)
                return 0;
            if (!vma_allows(current->mm, page, write, 0))
                return 0;
        } else {
            if (!(pde & PTE_USER))
                return 0;
            if (!pte_user(pte))
                return 0;
            if (write) {
                if (!(pde & PTE_READ_WRITE))
                    return 0;
                if (!pte_write(pte) && !(pte & PTE_COW))
                    return 0;
            }
        }
        if (page == last)
            break;
        page += PAGE_SIZE;
    }

    return 1;
}

static int resolve_cow(uint32_t page_base)
{
    pteval_t pte = get_pte_flags(page_directory, (void*)page_base);
    if (!(pte & PTE_COW))
        return 0;
    uint32_t phys = pte_pfn(pte);
    uint32_t rc = page_ref_count((unsigned long)phys);
    cow_faults++;
    if (rc <= 1) {
        pteval_t flags = (pte & (PAGE_SIZE - 1)) | PTE_READ_WRITE;
        flags &= ~PTE_COW;
        set_pte_flags(page_directory, (void*)page_base, flags);
        return 1;
    }
    void *new_phys = alloc_page(GFP_KERNEL);
    if (!new_phys)
        return -1;
    uint8_t *tmp = (uint8_t*)kmalloc(PAGE_SIZE);
    if (!tmp) {
        free_page((unsigned long)new_phys);
        return -1;
    }
    memcpy(tmp, (void*)page_base, PAGE_SIZE);
    map_page_ex(page_directory, new_phys, (void*)page_base, PTE_PRESENT | PTE_USER | PTE_READ_WRITE);
    if (current && current->mm) {
        rmap_remove(current->mm, page_base, phys);
        rmap_add(current->mm, page_base, (uint32_t)new_phys);
    }
    memcpy((void*)page_base, tmp, PAGE_SIZE);
    kfree(tmp);
    free_page((unsigned long)phys);
    cow_copies++;
    return 1;
}

int __copy_from_user(void* dst, const void* src_user, uint32_t len)
{
    if (!dst && len)
        return -1;
    if (!src_user && len)
        return -1;
    if (!access_ok(get_pgd_current(), (void*)src_user, len, 0))
        return -1;
    memcpy(dst, src_user, len);
    return 0;
}

int __copy_to_user(void* dst_user, const void* src, uint32_t len)
{
    if (!dst_user && len)
        return -1;
    if (!src && len)
        return -1;
    if (!access_ok(get_pgd_current(), (void*)dst_user, len, 1))
        return -1;

    if (len) {
        uint32_t start = (uint32_t)dst_user;
        uint32_t end = start + len - 1;
        uint32_t page = start & ~(PAGE_SIZE - 1);
        uint32_t last = end & ~(PAGE_SIZE - 1);
        while (1) {
            int res = resolve_cow(page);
            if (res < 0)
                return -1;
            if (page == last)
                break;
            page += PAGE_SIZE;
        }
    }
    memcpy(dst_user, src, len);
    return 0;
}

void get_cow_stats(uint32_t *faults, uint32_t *copies)
{
    if (faults) *faults = cow_faults;
    if (copies) *copies = cow_copies;
}

struct pt_regs *do_page_fault(struct pt_regs *regs)
{
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int is_present = regs->err_code & 0x1;
    int is_write = regs->err_code & 0x2;
    int is_user = regs->err_code & 0x4;
    int is_reserved = regs->err_code & 0x8;
    int is_instr_fetch = regs->err_code & 0x10;

    uint32_t page_base = faulting_address & ~(PAGE_SIZE - 1);

    if (is_reserved) {
        printf("Page Fault! ( reserved ) at 0x%x - EIP: 0x%x\n", faulting_address, regs->eip);
        panic("KERNEL PANIC: Page Fault caused by reserved bit violation!");
    }

    int user_access = is_user;
    if (!user_access && current && current->mm && faulting_address < TASK_SIZE)
        user_access = 1;

    if (is_present) {
        if (!user_access) {
            printf("Page Fault! ( present, kernel ) at 0x%x - EIP: 0x%x\n", faulting_address, regs->eip);
            panic("KERNEL PANIC: Unhandled Kernel Page Fault.");
        }

        if (is_write) {
            int res = resolve_cow(page_base);
            if (res > 0)
                return regs;
        }

        if (vma_allows(current ? current->mm : NULL, page_base, is_write, is_instr_fetch)) {
            pteval_t pte = get_pte_flags(page_directory, (void*)page_base);
            if (pte && (!pte_user(pte) || (is_write && !pte_write(pte)))) {
                void *phys = alloc_page(GFP_KERNEL);
                if (!phys)
                    panic("KERNEL PANIC: Out of physical memory in Page Fault handler.");
                pteval_t flags = PTE_PRESENT | PTE_USER;
                if (vma_allows(current ? current->mm : NULL, page_base, 1, 0))
                    flags |= PTE_READ_WRITE;
                map_page_ex(page_directory, phys, (void*)page_base, flags);
                if (current && current->mm)
                    rmap_add(current->mm, page_base, (uint32_t)phys);
                memset((void*)page_base, 0, PAGE_SIZE);
                return regs;
            }
        }

        printf("User Page Fault: unhandled protection fault.\n");
        do_exit_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        struct pt_regs *task_schedule(struct pt_regs *r);
        return task_schedule(regs);
    }

    if (faulting_address < PAGE_SIZE) {
        if (user_access) {
            printf("User Page Fault: null access.\n");
            do_exit_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
            struct pt_regs *task_schedule(struct pt_regs *r);
            return task_schedule(regs);
        }
        panic("KERNEL PANIC: Null pointer access.");
    }
    if (user_access && faulting_address >= TASK_SIZE) {
        printf("User Page Fault: kernel address at 0x%x - EIP: 0x%x\n", faulting_address, regs->eip);
        do_exit_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        struct pt_regs *task_schedule(struct pt_regs *r);
        return task_schedule(regs);
    }
    if (user_access && !vma_allows(current ? current->mm : NULL, page_base, is_write, is_instr_fetch)) {
        printf("User Page Fault: out of range at 0x%x - EIP: 0x%x\n", faulting_address, regs->eip);
        do_exit_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        struct pt_regs *task_schedule(struct pt_regs *r);
        return task_schedule(regs);
    }

    if (user_access && current && current->mm) {
        int swap_res = swap_in_mm(current->mm, page_base);
        if (swap_res > 0)
            return regs;
    }

    void *phys = alloc_page(GFP_KERNEL);
    if (!phys)
        panic("KERNEL PANIC: Out of physical memory in Page Fault handler.");

    pteval_t flags = PTE_PRESENT;
    if (user_access) {
        flags |= PTE_USER;
        if (vma_allows(current ? current->mm : NULL, page_base, 1, 0))
            flags |= PTE_READ_WRITE;
    } else {
        flags |= PTE_READ_WRITE;
    }
    map_page_ex(page_directory, phys, (void*)page_base, flags);
    if (user_access && current && current->mm)
        rmap_add(current->mm, page_base, (uint32_t)phys);

    memset((void*)page_base, 0, PAGE_SIZE);
    return regs;
}

void paging_init(void)
{
    uint32_t pgd_phys = (uint32_t)alloc_page(GFP_KERNEL);
    if (!pgd_phys)
        return (void)printf("PAGING: Failed to allocate Page Directory!\n");

    page_directory = (pgd_t*)phys_to_virt(pgd_phys);
    memset(page_directory, 0, PAGE_SIZE);

    uint32_t kernel_pde_base = pgd_index(PAGE_OFFSET);
    uint32_t lowmem_end = bootmem_lowmem_end();
    uint32_t low_pdes = (lowmem_end + PGDIR_SIZE - 1) / PGDIR_SIZE;
    if (low_pdes > (1024 - kernel_pde_base))
        low_pdes = 1024 - kernel_pde_base;
    for (uint32_t pde_idx = 0; pde_idx < low_pdes; pde_idx++) {
        uint32_t pt_phys = (uint32_t)alloc_page(GFP_KERNEL);
        if (!pt_phys)
            panic("PAGING PANIC: Failed to allocate Page Table!");
        pte_t* pt = (pte_t*)phys_to_virt(pt_phys);
        memset(pt, 0, PAGE_SIZE);

        for (uint32_t i = 0; i < PTRS_PER_PTE; i++) {
            uint32_t addr = (pde_idx * PTRS_PER_PTE + i) * PAGE_SIZE;
            pt[i] = addr | PTE_READ_WRITE | PTE_PRESENT;
        }

        page_directory[kernel_pde_base + pde_idx] = pt_phys | PTE_READ_WRITE | PTE_PRESENT;
    }

    register_interrupt_handler(14, do_page_fault);

    __asm__ volatile("mov %0, %%cr3" :: "r"(pgd_phys));
    kernel_directory = page_directory;

    printf("PAGING: Kernel mapped at PAGE_OFFSET (0-");
    printf("%d", low_pdes * (PGDIR_SIZE / (1024 * 1024)));
    printf("MB).\n");
}
