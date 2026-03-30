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
    uint32_t lowmem_end;
    uint32_t ram_kb;
    uint32_t reserved_kb;
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

static void bootmem_sort_available(void)
{
    for (uint32_t i = 0; i + 1 < bootmem.avail_count; i++) {
        for (uint32_t j = i + 1; j < bootmem.avail_count; j++) {
            if (bootmem.available[j].base < bootmem.available[i].base) {
                struct bootmem_region tmp = bootmem.available[i];
                bootmem.available[i] = bootmem.available[j];
                bootmem.available[j] = tmp;
            }
        }
    }
}

static uint32_t bootmem_min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t bootmem_max_u32(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static int bootmem_find_reserved_overlap(uint32_t base, uint32_t size, uint32_t *overlap_end)
{
    uint32_t end_addr = base + size;
    for (uint32_t i = 0; i < bootmem.resv_count; i++) {
        uint32_t rbase = bootmem.reserved[i].base;
        uint32_t rend = rbase + bootmem.reserved[i].size;
        if (end_addr <= rbase)
            continue;
        if (base >= rend)
            continue;
        if (overlap_end)
            *overlap_end = rend;
        return 1;
    }
    return 0;
}

void bootmem_init(struct multiboot_info *mbi)
{
    memset(&bootmem, 0, sizeof(bootmem));

    uint32_t kernel_end = (uint32_t)&end;
    if (kernel_end >= PAGE_OFFSET)
        kernel_end -= PAGE_OFFSET;
    uint32_t mods_struct_end = mbi->mods_addr + mbi->mods_count * sizeof(struct multiboot_module);
    if (mods_struct_end > kernel_end)
        kernel_end = mods_struct_end;
    struct multiboot_module* mod = (struct multiboot_module*)phys_to_virt(mbi->mods_addr);
    for (uint32_t i = 0; i < mbi->mods_count; i++)
        if (mod[i].mod_end > kernel_end)
            kernel_end = mod[i].mod_end;

    kernel_end = align_up(kernel_end, PAGE_SIZE);
    bootmem.start = kernel_end;
    bootmem.current = kernel_end;
    bootmem.end = kernel_end;
    bootmem_reserve(0, kernel_end);

    uint32_t max_ram_end = 0;
    uint32_t ram_kb = 0;
    uint32_t reserved_kb = 0;
    if (mbi->flags & (1 << 6)) {
        uint32_t mmap_phys = mbi->mmap_addr;
        uint32_t mmap_end = mmap_phys + mbi->mmap_length;
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);
        while (mmap_phys < mmap_end) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                bootmem_add_available(mmap->addr_low, mmap->len_low);
                uint32_t region_end = mmap->addr_low + mmap->len_low;
                if (region_end > max_ram_end)
                    max_ram_end = region_end;
                ram_kb += mmap->len_low / 1024;
            } else {
                reserved_kb += mmap->len_low / 1024;
            }
            mmap_phys += mmap->size + sizeof(mmap->size);
            mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);
        }
    }
    bootmem_sort_available();

    const uint32_t lowmem_limit = 0x38000000;
    if (max_ram_end == 0)
        max_ram_end = bootmem.start;
    bootmem.lowmem_end = bootmem_min_u32(max_ram_end, lowmem_limit);
    bootmem.end = bootmem.lowmem_end;
    bootmem.total_pages = bootmem.lowmem_end / PAGE_SIZE;
    bootmem.ram_kb = ram_kb;
    bootmem.reserved_kb = reserved_kb;

    if (bootmem.current < bootmem.start)
        bootmem.current = bootmem.start;
    if (bootmem.current > bootmem.end)
        bootmem.current = bootmem.end;
}

void *bootmem_alloc(uint32_t size, uint32_t align)
{
    uint32_t req_align = align ? align : PAGE_SIZE;
    if (size == 0)
        return NULL;

    for (uint32_t i = 0; i < bootmem.avail_count; i++) {
        uint32_t rbase = bootmem.available[i].base;
        uint32_t rend = rbase + bootmem.available[i].size;
        if (rbase >= bootmem.end)
            continue;
        if (rend > bootmem.end)
            rend = bootmem.end;

        uint32_t addr = align_up(bootmem_max_u32(bootmem.current, rbase), req_align);
        while (addr < rend) {
            if (addr + size > rend)
                break;
            uint32_t overlap_end = 0;
            if (bootmem_find_reserved_overlap(addr, size, &overlap_end)) {
                addr = align_up(overlap_end, req_align);
                continue;
            }
            bootmem.current = addr + size;
            bootmem_reserve(addr, size);
            return (void*)addr;
        }
    }
    return NULL;
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

uint32_t bootmem_lowmem_end(void)
{
    return bootmem.lowmem_end;
}

uint32_t bootmem_ram_kb(void)
{
    return bootmem.ram_kb;
}

uint32_t bootmem_reserved_kb(void)
{
    return bootmem.reserved_kb;
}
