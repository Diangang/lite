#include "vmm.h"
#include "pmm.h"
#include "libc.h"
#include "kernel.h"
#include "isr.h"
#include "task.h"
#include "kheap.h"

/*
 * 1024 Page Directory Entries.
 * Each entry points to a Page Table (or 4MB page).
 * We need this array to be 4KB aligned.
 */
static uint32_t* page_directory = NULL;
static uint32_t* kernel_directory = NULL;
static uint32_t cow_faults = 0;
static uint32_t cow_copies = 0;

/*
 * We will need at least one page table to map the first 4MB
 * (where kernel and BIOS/VGA reside).
 */

extern void load_page_directory(uint32_t*);
extern void enable_paging(void);

void vmm_map_page_ex(uint32_t* dir, void* phys_addr, void* virt_addr, uint32_t flags)
{
    if (!dir) return;

    uint32_t pde_idx = (uint32_t)virt_addr / (1024 * 4096);
    uint32_t pte_idx = ((uint32_t)virt_addr % (1024 * 4096)) / 4096;

    if (!(dir[pde_idx] & PTE_PRESENT)) {
        uint32_t* new_table = (uint32_t*)pmm_alloc_page();
        if (!new_table)
            return (void)printf("VMM: Failed to allocate Page Table for 0x%x\n", virt_addr);

        memset(new_table, 0, 4096);
        dir[pde_idx] = ((uint32_t)new_table) | PTE_READ_WRITE | PTE_PRESENT;
        if (flags & PTE_USER)
            dir[pde_idx] |= PTE_USER;

        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
    } else if (flags & PTE_USER) {
        dir[pde_idx] |= PTE_USER;
    }

    uint32_t* table = (uint32_t*)(dir[pde_idx] & ~0xFFF);
    table[pte_idx] = ((uint32_t)phys_addr) | flags;

    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void vmm_map_page(void* phys_addr, void* virt_addr)
{
    vmm_map_page_ex(page_directory, phys_addr, virt_addr, PTE_READ_WRITE | PTE_PRESENT);
}

int vmm_is_mapped(void* virt_addr)
{
    if (!page_directory) return 0;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = page_directory[pde_idx];
    if (!(pde & PTE_PRESENT)) return 0;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];

    return (pte & PTE_PRESENT) ? 1 : 0;
}

uint32_t vmm_virt_to_phys(void* virt_addr)
{
    if (!page_directory) return 0xFFFFFFFF;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = page_directory[pde_idx];
    if (!(pde & PTE_PRESENT)) return 0xFFFFFFFF;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return 0xFFFFFFFF;

    return (pte & ~0xFFF) + (va & 0xFFF);
}

uint32_t vmm_virt_to_phys_ex(uint32_t* dir, void* virt_addr)
{
    if (!dir) return 0xFFFFFFFF;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return 0xFFFFFFFF;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return 0xFFFFFFFF;

    return (pte & ~0xFFF) + (va & 0xFFF);
}

uint32_t vmm_get_pte_flags_ex(uint32_t* dir, void* virt_addr)
{
    if (!dir) return 0;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return 0;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return 0;

    return pte;
}

void vmm_update_page_flags_ex(uint32_t* dir, void* virt_addr, uint32_t flags)
{
    if (!dir) return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return;

    if (flags & PTE_USER) {
        dir[pde_idx] |= PTE_USER;
    }

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return;

    table[pte_idx] = (pte & ~0xFFF) | (flags & 0xFFF);
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void vmm_set_page_user(void* virt_addr)
{
    vmm_set_page_user_ex(page_directory, virt_addr);
}

void vmm_set_page_user_ex(uint32_t* dir, void* virt_addr)
{
    if (!dir) return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return;

    dir[pde_idx] |= PTE_USER;
    table[pte_idx] = pte | PTE_USER;

    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

void vmm_set_page_readonly_ex(uint32_t* dir, void* virt_addr)
{
    if (!dir) return;

    uint32_t va = (uint32_t)virt_addr;
    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;

    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return;

    table[pte_idx] = pte & ~PTE_READ_WRITE;
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
}

uint32_t* vmm_clone_kernel_directory(void)
{
    if (!kernel_directory) return NULL;
    uint32_t* new_dir = (uint32_t*)pmm_alloc_page();
    if (!new_dir) return NULL;
    memset(new_dir, 0, 4096);

    for (int i = 0; i < 1024; i++) {
        new_dir[i] = kernel_directory[i];
    }

    return new_dir;
}

void vmm_switch_directory(uint32_t* dir)
{
    if (!dir) return;
    page_directory = dir;
    __asm__ volatile("mov %0, %%cr3" :: "r"(dir));
}

uint32_t* vmm_get_current_directory(void)
{
    return page_directory;
}

uint32_t* vmm_get_kernel_directory(void)
{
    return kernel_directory;
}

static int vmm_get_pte_ex(uint32_t* dir, uint32_t va, uint32_t* out_pde, uint32_t* out_pte)
{
    if (!dir) return 0;

    uint32_t pde_idx = va / (1024 * 4096);
    uint32_t pte_idx = (va % (1024 * 4096)) / 4096;
    uint32_t pde = dir[pde_idx];
    if (!(pde & PTE_PRESENT)) return 0;

    uint32_t* table = (uint32_t*)(pde & ~0xFFF);
    uint32_t pte = table[pte_idx];
    if (!(pte & PTE_PRESENT)) return 0;

    if (out_pde) *out_pde = pde;
    if (out_pte) *out_pte = pte;
    return 1;
}

int vmm_user_accessible(uint32_t* dir, void* addr, uint32_t len, int write)
{
    if (!dir) return 0;
    if (len == 0) return 1;

    uint32_t start = (uint32_t)addr;
    uint32_t end = start + len - 1;

    if (start < 0x1000) return 0;
    if (end < start) return 0;
    if (end >= 0xC0000000) return 0;

    uint32_t page = start & ~0xFFF;
    uint32_t last = end & ~0xFFF;
    while (1) {
        uint32_t pde, pte;
        if (!vmm_get_pte_ex(dir, page, &pde, &pte)) return 0;
        if (!(pde & PTE_USER)) return 0;
        if (!(pte & PTE_USER)) return 0;
        if (write) {
            if (!(pde & PTE_READ_WRITE)) return 0;
            if (!(pte & PTE_READ_WRITE) && !(pte & PTE_COW)) return 0;
        }
        if (page == last) break;
        page += 4096;
    }

    return 1;
}

static int vmm_resolve_cow(uint32_t page_base)
{
    uint32_t pte = vmm_get_pte_flags_ex(page_directory, (void*)page_base);
    if (!(pte & PTE_COW)) return 0;
    uint32_t phys = pte & ~0xFFF;
    uint32_t rc = pmm_get_refcount((void*)phys);
    cow_faults++;
    if (rc <= 1) {
        uint32_t flags = (pte & 0xFFF) | PTE_READ_WRITE;
        flags &= ~PTE_COW;
        vmm_update_page_flags_ex(page_directory, (void*)page_base, flags);
        return 1;
    }
    void *new_phys = pmm_alloc_page();
    if (!new_phys) return -1;
    uint8_t *tmp = (uint8_t*)kmalloc(4096);
    if (!tmp) {
        pmm_free_page(new_phys);
        return -1;
    }
    memcpy(tmp, (void*)page_base, 4096);
    vmm_map_page_ex(page_directory, new_phys, (void*)page_base, PTE_PRESENT | PTE_USER | PTE_READ_WRITE);
    memcpy((void*)page_base, tmp, 4096);
    kfree(tmp);
    pmm_free_page((void*)phys);
    cow_copies++;
    return 1;
}

int vmm_copyin(void* dst, const void* src_user, uint32_t len)
{
    if (!dst && len) return -1;
    if (!src_user && len) return -1;
    if (!vmm_user_accessible(vmm_get_current_directory(), (void*)src_user, len, 0)) return -1;
    memcpy(dst, src_user, len);
    return 0;
}

int vmm_copyout(void* dst_user, const void* src, uint32_t len)
{
    if (!dst_user && len) return -1;
    if (!src && len) return -1;
    if (!vmm_user_accessible(vmm_get_current_directory(), (void*)dst_user, len, 1)) return -1;

    if (len) {
        uint32_t start = (uint32_t)dst_user;
        uint32_t end = start + len - 1;
        uint32_t page = start & ~0xFFF;
        uint32_t last = end & ~0xFFF;
        while (1) {
            int res = vmm_resolve_cow(page);
            if (res < 0) return -1;
            if (page == last) break;
            page += 4096;
        }
    }
    memcpy(dst_user, src, len);
    return 0;
}

void vmm_get_cow_stats(uint32_t *faults, uint32_t *copies)
{
    if (faults) *faults = cow_faults;
    if (copies) *copies = cow_copies;
}

struct registers *page_fault_handler(struct registers *regs)
{
    /* The faulting address is stored in the CR2 register */
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int is_present = regs->err_code & 0x1;
    int is_write = regs->err_code & 0x2;
    int is_user = regs->err_code & 0x4;
    int is_reserved = regs->err_code & 0x8;
    int is_instr_fetch = regs->err_code & 0x10;

    printf("Page Fault! ( ");
    if (!is_present) printf("not-present ");
    if (is_write) printf("write ");
    else printf("read ");
    if (is_user) printf("user ");
    else printf("kernel ");
    if (is_reserved) printf("reserved ");
    if (is_instr_fetch) printf("instruction-fetch ");
    printf(") at 0x%x - EIP: 0x%x\n", faulting_address, regs->eip);
    uint32_t page_base = faulting_address & 0xFFFFF000;

    /* Guard clause: Reserved bit violation is a fatal kernel bug */
    if (is_reserved)
        panic("KERNEL PANIC: Page Fault caused by reserved bit violation!");

    if (is_present) {
        /* Guard clause: Unhandled kernel page fault */
        if (!is_user)
            panic("KERNEL PANIC: Unhandled Kernel Page Fault.");

        /* Handle User Protection Faults (Present, but permission denied) */
        /* Try to resolve Copy-On-Write first */
        if (is_write) {
            int res = vmm_resolve_cow(page_base);
            if (res > 0)
                return regs;
        }

        /* Check if we can remap it (e.g., missed user/write flags during lazy alloc) */
        if (task_user_vma_allows(page_base, is_write, is_instr_fetch)) {
            uint32_t pte = vmm_get_pte_flags_ex(page_directory, (void*)page_base);
            if (pte && (!(pte & PTE_USER) || (is_write && !(pte & PTE_READ_WRITE)))) {
                void *phys = pmm_alloc_page();
                if (!phys)
                    panic("KERNEL PANIC: Out of physical memory in Page Fault handler.");
                uint32_t flags = PTE_PRESENT | PTE_USER;
                if (task_user_vma_allows(page_base, 1, 0))
                    flags |= PTE_READ_WRITE;
                vmm_map_page_ex(page_directory, phys, (void*)page_base, flags);
                memset((void*)page_base, 0, 4096);
                printf("Page Fault handled: remapped 0x%x -> 0x%x\n", page_base, (uint32_t)phys);
                return regs;
            }
        }

        /* If we reach here, it's a genuine protection fault we can't handle */
        printf("User Page Fault: unhandled protection fault.\n");
        task_exit_with_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        return regs;
    }

    /* Handle Demand Paging (Not Present) */
    if (faulting_address < 0x1000) {
        if (is_user) {
            printf("User Page Fault: null access.\n");
            task_exit_with_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
            return regs;
        }
        panic("KERNEL PANIC: Null pointer access.");
    }
    if (is_user && faulting_address >= 0xC0000000) {
        printf("User Page Fault: kernel address.\n");
        task_exit_with_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        return regs;
    }
    if (is_user && !task_user_vma_allows(page_base, is_write, is_instr_fetch)) {
        printf("User Page Fault: out of range.\n");
        task_exit_with_reason(1, TASK_EXIT_PAGEFAULT, faulting_address, regs->eip);
        return regs;
    }

    void *phys = pmm_alloc_page();
    if (!phys)
        panic("KERNEL PANIC: Out of physical memory in Page Fault handler.");

    uint32_t flags = PTE_PRESENT;
    if (is_user) {
        flags |= PTE_USER;
        if (task_user_vma_allows(page_base, 1, 0))
            flags |= PTE_READ_WRITE;
    } else {
        flags |= PTE_READ_WRITE;
    }
    vmm_map_page_ex(page_directory, phys, (void*)page_base, flags);

    memset((void*)page_base, 0, 4096);
    printf("Page Fault handled: mapped 0x%x -> 0x%x\n", page_base, (uint32_t)phys);
    return regs;
}

void vmm_init(void)
{
    /* 1. Allocate a page for the Page Directory */
    page_directory = (uint32_t*)pmm_alloc_page();
    if (!page_directory)
        return (void)printf("VMM: Failed to allocate Page Directory!\n");

    /* Clear it - Mark all PDEs as not present */
    memset(page_directory, 0, 4096);

    /* 2. Identity Map the first 128MB (Physical 0-128MB -> Virtual 0-128MB) */
    /* This ensures kernel, initrd, and low memory are all accessible */
    /* We need 32 page tables to cover 128MB (128 / 4 = 32) */

    /* Loop through 32 Page Tables (covering 128MB) */
    for (int pde_idx = 0; pde_idx < 32; pde_idx++) {
        uint32_t* pt = (uint32_t*)pmm_alloc_page();
        if (!pt)
            panic("VMM PANIC: Failed to allocate Page Table!");
        memset(pt, 0, 4096);

        /* Fill the page table */
        for (int i = 0; i < 1024; i++) {
            /* Attribute: Supervisor, Read/Write, Present */
            uint32_t addr = (pde_idx * 1024 + i) * 4096;
            pt[i] = addr | PTE_READ_WRITE | PTE_PRESENT;
        }

        /* Put the Page Table into the Page Directory */
        page_directory[pde_idx] = ((uint32_t)pt) | PTE_READ_WRITE | PTE_PRESENT;
    }

    /* 3. Register Page Fault Handler */
    register_interrupt_handler(14, page_fault_handler);

    /* 6. Load CR3 and Enable Paging (CR0) */
    /* We use inline assembly here for CR3 loading and CR0 setting */
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_directory));
    kernel_directory = page_directory;

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* Set PG bit (31) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    printf("VMM: Paging Enabled! Identity mapped 0-4MB.\n");
}