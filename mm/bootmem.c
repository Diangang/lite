#include "linux/bootmem.h"
#include "asm/multiboot.h"
#include "asm/page.h"
#include "linux/libc.h"
#include "linux/memlayout.h"

#define BOOTMEM_MAX_REGIONS 256
#define BOOTMEM_MAX_E820 64
#define BOOTMEM_MAX_MODULES 8

struct bootmem_region {
    uint32_t base;
    uint32_t size;
};

struct bootmem_e820_entry {
    uint32_t base;
    uint32_t size;
    uint32_t type;
};

struct bootmem_module_range {
    uint32_t start;
    uint32_t end;
};

struct bootmem_data {
    struct bootmem_region available[BOOTMEM_MAX_REGIONS];
    struct bootmem_region reserved[BOOTMEM_MAX_REGIONS];
    struct bootmem_e820_entry e820[BOOTMEM_MAX_E820];
    struct bootmem_module_range mods[BOOTMEM_MAX_MODULES];
    uint32_t avail_count;
    uint32_t resv_count;
    uint32_t e820_count;
    uint32_t mods_count;
    uint32_t kernel_phys_start;
    uint32_t kernel_phys_end;
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

static uint32_t align_down(uint32_t value, uint32_t align)
{
    if (align == 0)
        return value;
    uint32_t mask = align - 1;
    return value & ~mask;
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

static void bootmem_add_e820(uint32_t base, uint32_t size, uint32_t type)
{
    if (size == 0)
        return;
    if (bootmem.e820_count >= BOOTMEM_MAX_E820)
        return;
    bootmem.e820[bootmem.e820_count].base = base;
    bootmem.e820[bootmem.e820_count].size = size;
    bootmem.e820[bootmem.e820_count].type = type;
    bootmem.e820_count++;
}

static void bootmem_sort_e820(void)
{
    for (uint32_t i = 0; i + 1 < bootmem.e820_count; i++) {
        for (uint32_t j = i + 1; j < bootmem.e820_count; j++) {
            if (bootmem.e820[j].base < bootmem.e820[i].base) {
                struct bootmem_e820_entry tmp = bootmem.e820[i];
                bootmem.e820[i] = bootmem.e820[j];
                bootmem.e820[j] = tmp;
            }
        }
    }
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
    kernel_end = align_up(kernel_end, PAGE_SIZE);

    bootmem.kernel_phys_start = 0x00100000;
    bootmem.kernel_phys_end = kernel_end;

    bootmem_reserve(0, 0x00100000);
    if (bootmem.kernel_phys_end > bootmem.kernel_phys_start)
        bootmem_reserve(bootmem.kernel_phys_start, bootmem.kernel_phys_end - bootmem.kernel_phys_start);

    if (mbi->mods_count && mbi->mods_addr) {
        uint32_t mods_struct_size = mbi->mods_count * sizeof(struct multiboot_module);
        bootmem_reserve(mbi->mods_addr, mods_struct_size);

        struct multiboot_module *mod = (struct multiboot_module*)memlayout_directmap_phys_to_virt(mbi->mods_addr);
        uint32_t take = mbi->mods_count;
        if (take > BOOTMEM_MAX_MODULES)
            take = BOOTMEM_MAX_MODULES;
        for (uint32_t i = 0; i < take; i++) {
            bootmem.mods[i].start = mod[i].mod_start;
            bootmem.mods[i].end = mod[i].mod_end;
            if (mod[i].mod_end > mod[i].mod_start)
                bootmem_reserve(mod[i].mod_start, mod[i].mod_end - mod[i].mod_start);
        }
        bootmem.mods_count = take;
    }

    bootmem.start = kernel_end;
    bootmem.current = kernel_end;
    bootmem.end = kernel_end;

    uint32_t max_ram_end = 0;
    uint32_t ram_kb = 0;
    uint32_t reserved_kb = 0;
    if (mbi->flags & (1 << 6)) {
        uint32_t mmap_phys = mbi->mmap_addr;
        uint32_t mmap_end = mmap_phys + mbi->mmap_length;
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);
        while (mmap_phys < mmap_end) {
            bootmem_add_e820(mmap->addr_low, mmap->len_low, mmap->type);
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
            mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);
        }
    }
    bootmem_sort_available();
    bootmem_sort_e820();

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

uint32_t bootmem_present_pages(uint32_t start_pfn, uint32_t end_pfn)
{
    if (end_pfn > bootmem.total_pages)
        end_pfn = bootmem.total_pages;
    if (start_pfn >= end_pfn)
        return 0;

    uint32_t start_addr = start_pfn * PAGE_SIZE;
    uint32_t end_addr = end_pfn * PAGE_SIZE;
    if (start_addr >= bootmem.lowmem_end)
        return 0;
    if (end_addr > bootmem.lowmem_end)
        end_addr = bootmem.lowmem_end;

    uint32_t pages = 0;
    for (uint32_t i = 0; i < bootmem.avail_count; i++) {
        uint32_t rbase = bootmem.available[i].base;
        uint32_t rend = rbase + bootmem.available[i].size;
        if (rend <= start_addr)
            continue;
        if (rbase >= end_addr)
            break;
        uint32_t a = bootmem_max_u32(rbase, start_addr);
        uint32_t b = bootmem_min_u32(rend, end_addr);
        a = align_up(a, PAGE_SIZE);
        b = align_down(b, PAGE_SIZE);
        if (b > a)
            pages += (b - a) / PAGE_SIZE;
    }
    return pages;
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

uint32_t bootmem_e820_entries(void)
{
    return bootmem.e820_count;
}

int bootmem_e820_get(uint32_t idx, uint32_t *base, uint32_t *size, uint32_t *type)
{
    if (idx >= bootmem.e820_count)
        return -1;
    if (base)
        *base = bootmem.e820[idx].base;
    if (size)
        *size = bootmem.e820[idx].size;
    if (type)
        *type = bootmem.e820[idx].type;
    return 0;
}

uint32_t bootmem_kernel_phys_start(void)
{
    return bootmem.kernel_phys_start;
}

uint32_t bootmem_kernel_phys_end(void)
{
    return bootmem.kernel_phys_end;
}

uint32_t bootmem_module_count(void)
{
    return bootmem.mods_count;
}

int bootmem_module_get(uint32_t idx, uint32_t *start, uint32_t *end)
{
    if (idx >= bootmem.mods_count)
        return -1;
    if (start)
        *start = bootmem.mods[idx].start;
    if (end)
        *end = bootmem.mods[idx].end;
    return 0;
}
