#include "linux/bootmem.h"
#include "asm/multiboot.h"
#include "asm/page.h"
#include "linux/libc.h"

#define BOOTMEM_MAX_REGIONS 32

struct bootmem_region {
    uint32_t base;
    uint32_t size;
};

struct bootmem_data {
    struct bootmem_region available[BOOTMEM_MAX_REGIONS];
    struct bootmem_region reserved[BOOTMEM_MAX_REGIONS];
    uint32_t avail_count;
    uint32_t resv_count;
    uint32_t total_pages;
    uint32_t start;
    uint32_t end;
    uint32_t current;
};

extern uint32_t end;

static struct bootmem_data bootmem;

static uint32_t align_up(uint32_t value, uint32_t align)
{
    if (align == 0)
        return value;
    uint32_t mask = align - 1;
    return (value + mask) & ~mask;
}

void bootmem_reserve(uint32_t base, uint32_t size)
{
    if (bootmem.resv_count >= BOOTMEM_MAX_REGIONS)
        return;
    bootmem.reserved[bootmem.resv_count].base = base;
    bootmem.reserved[bootmem.resv_count].size = size;
    bootmem.resv_count++;
}

int bootmem_is_reserved(uint32_t base, uint32_t size)
{
    uint32_t end_addr = base + size;
    for (uint32_t i = 0; i < bootmem.resv_count; i++) {
        uint32_t rbase = bootmem.reserved[i].base;
        uint32_t rend = rbase + bootmem.reserved[i].size;
        if (end_addr <= rbase)
            continue;
        if (base >= rend)
            continue;
        return 1;
    }
    return 0;
}

static void bootmem_add_available(uint32_t base, uint32_t size)
{
    if (bootmem.avail_count >= BOOTMEM_MAX_REGIONS)
        return;
    bootmem.available[bootmem.avail_count].base = base;
    bootmem.available[bootmem.avail_count].size = size;
    bootmem.avail_count++;
}

void bootmem_init(struct multiboot_info *mbi)
{
    memset(&bootmem, 0, sizeof(bootmem));
    if (mbi->flags & 1)
        bootmem.total_pages = ((mbi->mem_lower + mbi->mem_upper) * 1024) / PAGE_SIZE;

    uint32_t kernel_end = (uint32_t)&end;
    uint32_t mods_struct_end = mbi->mods_addr + mbi->mods_count * sizeof(struct multiboot_module);
    if (mods_struct_end > kernel_end)
        kernel_end = mods_struct_end;
    struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++)
        if (mod[i].mod_end > kernel_end)
            kernel_end = mod[i].mod_end;

    kernel_end = align_up(kernel_end, PAGE_SIZE);
    bootmem.start = kernel_end;
    bootmem.current = kernel_end;
    bootmem.end = kernel_end;
    bootmem_reserve(0, kernel_end);

    if (mbi->flags & (1 << 6)) {
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)mbi->mmap_addr;
        while ((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                bootmem_add_available(mmap->addr_low, mmap->len_low);
                uint32_t region_end = mmap->addr_low + mmap->len_low;
                if (region_end > bootmem.end)
                    bootmem.end = region_end;
            }
            mmap = (struct multiboot_memory_map*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    }
    if (bootmem.end == 0)
        bootmem.end = bootmem.start;
}

void *bootmem_alloc(uint32_t size, uint32_t align)
{
    uint32_t addr = align_up(bootmem.current, align ? align : PAGE_SIZE);
    if (addr + size > bootmem.end)
        return NULL;
    bootmem.current = addr + size;
    bootmem_reserve(addr, size);
    return (void*)addr;
}

uint32_t bootmem_start(void)
{
    return bootmem.start;
}

uint32_t bootmem_end(void)
{
    return bootmem.end;
}

uint32_t bootmem_total_pages(void)
{
    return bootmem.total_pages;
}
