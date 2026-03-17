#include "vmm.h"
#include "pmm.h"
#include "libc.h"
#include "kernel.h"
#include "isr.h"

/*
 * 1024 Page Directory Entries.
 * Each entry points to a Page Table (or 4MB page).
 * We need this array to be 4KB aligned.
 */
static uint32_t* page_directory = NULL;

/*
 * We will need at least one page table to map the first 4MB
 * (where kernel and BIOS/VGA reside).
 */
static uint32_t* first_page_table = NULL;

extern void load_page_directory(uint32_t*);
extern void enable_paging(void);

void vmm_init(void)
{
    /* 1. Allocate a page for the Page Directory */
    page_directory = (uint32_t*)pmm_alloc_page();
    if (!page_directory) {
        printf("VMM: Failed to allocate Page Directory!\n");
        return;
    }

    /* Clear it - Mark all PDEs as not present */
    memset(page_directory, 0, 4096);

    /* 2. Allocate a page for the first Page Table (0-4MB) */
    first_page_table = (uint32_t*)pmm_alloc_page();
    if (!first_page_table) {
        printf("VMM: Failed to allocate Page Table!\n");
        return;
    }

    /* 3. Identity Map the first 128MB (Physical 0-128MB -> Virtual 0-128MB) */
    /* This ensures kernel, initrd, and low memory are all accessible */
    /* We need 32 page tables to cover 128MB (128 / 4 = 32) */

    uint32_t addr = 0;

    /* Loop through 32 Page Tables (covering 128MB) */
    for (int pde_idx = 0; pde_idx < 32; pde_idx++) {
        uint32_t* pt;

        if (pde_idx == 0) {
            pt = first_page_table; /* We already allocated this one */
        } else {
            pt = (uint32_t*)pmm_alloc_page();
            if (!pt) {
                printf("VMM PANIC: Failed to allocate Page Table %d! Halting.\n", pde_idx);
                for(;;); /* Halt */
            }
            memset(pt, 0, 4096);
        }

        /* Fill the page table */
        for (int i = 0; i < 1024; i++) {
            /* Attribute: Supervisor, Read/Write, Present */
            pt[i] = addr | PTE_READ_WRITE | PTE_PRESENT;
            addr += 4096;
        }

        /* Put the Page Table into the Page Directory */
        page_directory[pde_idx] = ((uint32_t)pt) | PTE_READ_WRITE | PTE_PRESENT;
    }

    /* 5. Register Page Fault Handler */
    register_interrupt_handler(14, page_fault_handler);

    /* 6. Load CR3 and Enable Paging (CR0) */
    /* We use inline assembly here for CR3 loading and CR0 setting */
    __asm__ volatile("mov %0, %%cr3" :: "r"(page_directory));

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; /* Set PG bit (31) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    printf("VMM: Paging Enabled! Identity mapped 0-4MB.\n");
}

void vmm_map_page(void* phys_addr, void* virt_addr)
{
    /* printf("DEBUG: Mapping phys 0x%x -> virt 0x%x\n", phys_addr, virt_addr); */

    /*
     * Simple mapper: Assumes the page table for this address already exists.
     * Since our heap is at 0xC0000000 (3GB mark), we need a page table for that region.
     * PDE index for 0xC0000000 is 768.
     */

    uint32_t pde_idx = (uint32_t)virt_addr / (1024 * 4096);
    uint32_t pte_idx = ((uint32_t)virt_addr % (1024 * 4096)) / 4096;

    /* Check if the page table exists */
    if (!(page_directory[pde_idx] & PTE_PRESENT)) {
        /* Create a new page table */
        uint32_t* new_table = (uint32_t*)pmm_alloc_page();
        if (!new_table) {
            printf("VMM: Failed to allocate Page Table for 0x%x\n", virt_addr);
            return;
        }

        memset(new_table, 0, 4096);

        /* Add to directory */
        page_directory[pde_idx] = ((uint32_t)new_table) | PTE_READ_WRITE | PTE_PRESENT;

        /*
         * CRITICAL: We just modified the page directory, but CR3 is already loaded.
         * The CPU caches PDE/PTEs in the TLB (Translation Lookaside Buffer).
         * We MUST flush the TLB for this to take effect.
         * Reloading CR3 flushes the entire TLB.
         */
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
    }

    /* Get the page table address from the directory (mask out flags) */
    uint32_t* table = (uint32_t*)(page_directory[pde_idx] & ~0xFFF);

    /* Map the page */
    table[pte_idx] = ((uint32_t)phys_addr) | PTE_READ_WRITE | PTE_PRESENT;

    /* Flush TLB for this specific page */
    __asm__ volatile("invlpg (%0)" :: "r" (virt_addr) : "memory");
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

void page_fault_handler(registers_t *regs)
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

    if (!is_present && !is_reserved) {
        if (faulting_address < 0x1000) {
            printf("KERNEL PANIC: Null pointer access.\n");
            for(;;);
        }

        void *phys = pmm_alloc_page();
        if (!phys) {
            printf("KERNEL PANIC: Out of physical memory in Page Fault handler.\n");
            for(;;);
        }

        uint32_t page_base = faulting_address & 0xFFFFF000;
        vmm_map_page(phys, (void*)page_base);

        uint32_t flags = PTE_READ_WRITE | PTE_PRESENT;
        if (is_user) flags |= PTE_USER;

        uint32_t pde_idx = page_base / (1024 * 4096);
        uint32_t pte_idx = (page_base % (1024 * 4096)) / 4096;
        uint32_t* table = (uint32_t*)(page_directory[pde_idx] & ~0xFFF);
        table[pte_idx] = ((uint32_t)phys) | flags;

        __asm__ volatile("invlpg (%0)" :: "r" ((void*)page_base) : "memory");

        memset((void*)page_base, 0, 4096);
        printf("Page Fault handled: mapped 0x%x -> 0x%x\n", page_base, (uint32_t)phys);
        return;
    }

    printf("KERNEL PANIC: Unhandled Page Fault.\n");
    for(;;);
}
