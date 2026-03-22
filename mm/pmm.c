#include "pmm.h"
#include "libc.h"

static struct multiboot_info* cached_mbi = NULL;
static uint32_t total_memory_kb = 0;
static uint32_t total_pages = 0;
static uint32_t* pmm_bitmap = NULL;
static uint32_t bitmap_size = 0;
static uint8_t* pmm_refcount = NULL;
static uint32_t refcount_size = 0;

/* Bitmap Helpers */
static inline void bitmap_set(uint32_t bit)
{
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_unset(uint32_t bit)
{
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline int bitmap_test(uint32_t bit)
{
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

/* Find first free page (first bit that is 0) */
static int bitmap_first_free(void)
{
    for (uint32_t i = 0; i < bitmap_size; i++) {
        if (pmm_bitmap[i] == 0xFFFFFFFF)
            continue;

        for (int j = 0; j < 32; j++)
            if (!(pmm_bitmap[i] & (1 << j)))
                return i * 32 + j;
    }

    return -1;
}

void* pmm_alloc_page(void)
{
    int frame = bitmap_first_free();
    if (frame == -1)
        return printf("PMM: Out of memory!\n"), NULL;

    bitmap_set(frame);
    if (pmm_refcount && frame >= 0 && (uint32_t)frame < total_pages)
        pmm_refcount[frame] = 1;

    uint32_t addr = (uint32_t)frame * PMM_PAGE_SIZE;
    return (void*)addr;
}

void pmm_free_page(void* p)
{
    uint32_t addr = (uint32_t)p;
    uint32_t frame = addr / PMM_PAGE_SIZE;

    if (pmm_refcount && frame < total_pages) {
        uint8_t rc = pmm_refcount[frame];
        if (rc > 1) {
            pmm_refcount[frame] = rc - 1;
            return;
        }

        if (rc == 1)
            pmm_refcount[frame] = 0;
    }
    bitmap_unset(frame);
}

void pmm_ref_page(void* p)
{
    uint32_t addr = (uint32_t)p;
    uint32_t frame = addr / PMM_PAGE_SIZE;

    if (!pmm_refcount || frame >= total_pages)
        return;

    if (pmm_refcount[frame] == 0)
        pmm_refcount[frame] = 1;
    else if (pmm_refcount[frame] < 0xFF)
        pmm_refcount[frame]++;
}

uint32_t pmm_get_refcount(void* p)
{
    uint32_t addr = (uint32_t)p;
    uint32_t frame = addr / PMM_PAGE_SIZE;

    if (!pmm_refcount || frame >= total_pages)
        return 0;

    return pmm_refcount[frame];
}

void pmm_print_info(void)
{
    if (!cached_mbi)
        return (void)printf("PMM not initialized or Multiboot info not found.\n");

    if (cached_mbi->flags & 1)
        printf("Total Memory: %d KB (%d MB)\n", total_memory_kb, total_memory_kb / 1024);

    if (cached_mbi->flags & (1 << 6)) {
        printf("Memory Map provided by BIOS:\n");
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)cached_mbi->mmap_addr;

        while ((uint32_t)mmap < cached_mbi->mmap_addr + cached_mbi->mmap_length) {
            const char* type_str = "Unknown";
            switch (mmap->type) {
            case MULTIBOOT_MEMORY_AVAILABLE:
                type_str = "Available RAM";
                break;
            case MULTIBOOT_MEMORY_RESERVED:
                type_str = "Reserved (Hardware)";
                break;
            case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                type_str = "ACPI Reclaimable";
                break;
            case MULTIBOOT_MEMORY_NVS:
                type_str = "ACPI NVS";
                break;
            case MULTIBOOT_MEMORY_BADRAM:
                type_str = "Bad RAM";
                break;
            }

            /* We only print the low 32-bits for now since it's a 32-bit OS */
            uint32_t size_kb = mmap->len_low / 1024;

            printf("  [0x%x - 0x%x] %d KB (%s)\n",
                   mmap->addr_low,
                   mmap->addr_low + mmap->len_low - 1,
                   size_kb, type_str);

            /* Move to the next entry. The size field does not include the size field itself (4 bytes) */
            mmap = (struct multiboot_memory_map*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    } else {
        printf("BIOS did not provide a memory map.\n");
    }
}

uint32_t pmm_get_total_kb(void)
{
    return total_memory_kb;
}

uint32_t pmm_get_free_kb(void)
{
    if (!pmm_bitmap || total_pages == 0)
        return 0;

    uint32_t free_pages = 0;
    uint32_t bits = total_pages;

    for (uint32_t i = 0; i < bitmap_size; i++) {
        uint32_t v = pmm_bitmap[i];
        if (v == 0xFFFFFFFF) {
            bits -= bits >= 32 ? 32 : bits;
            continue;
        }
        for (uint32_t j = 0; j < 32 && bits > 0; j++) {
            if (!(v & (1u << j)))
                free_pages++;
            bits--;
        }
    }

    return free_pages * (PMM_PAGE_SIZE / 1024);
}

extern uint32_t end; /* Defined in linker.ld, end of kernel */
void init_pmm(struct multiboot_info* mbi)
{
    cached_mbi = mbi;

    /* 1. Calculate total memory */
    if (mbi->flags & 1)
        total_memory_kb = mbi->mem_lower + mbi->mem_upper;

    total_pages = (total_memory_kb * 1024) / PMM_PAGE_SIZE;

    /* 2. Place bitmap after the kernel AND after any modules */
    /* We use the 'end' symbol from linker script to find where kernel ends */
    uint32_t kernel_end = (uint32_t)&end;

    /* Check module structure array location */
    uint32_t mods_struct_end = mbi->mods_addr + mbi->mods_count * sizeof(struct multiboot_module);
    if (mods_struct_end > kernel_end)
        kernel_end = mods_struct_end;

    /* Check module data location */
    struct multiboot_module* mod = (struct multiboot_module*)mbi->mods_addr;
    for (uint32_t i = 0; i < mbi->mods_count; i++)
        if (mod[i].mod_end > kernel_end)
            kernel_end = mod[i].mod_end;

    /* Align kernel_end to page boundary */
    if (kernel_end % PMM_PAGE_SIZE)
        kernel_end += PMM_PAGE_SIZE - (kernel_end % PMM_PAGE_SIZE);

    pmm_bitmap = (uint32_t*)kernel_end;
    bitmap_size = total_pages / 32;
    if (total_pages % 32)
        bitmap_size++;
    pmm_refcount = (uint8_t*)((uint8_t*)pmm_bitmap + bitmap_size * sizeof(uint32_t));
    refcount_size = total_pages * sizeof(uint8_t);

    /* Initialize bitmap: Mark ALL pages as used first (safe default) */
    memset(pmm_bitmap, 0xFF, bitmap_size * 4);
    memset(pmm_refcount, 0, refcount_size);

    /* 3. Parse memory map to free available pages */
    if (!(mbi->flags & (1 << 6)))
        panic("PMM PANIC: BIOS did not provide a memory map!");

    uint32_t meta_end_addr = (uint32_t)pmm_refcount + refcount_size;
    struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)mbi->mmap_addr;
    while ((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
        if (mmap->type != MULTIBOOT_MEMORY_AVAILABLE)
            goto next;

        /* Don't free low memory (0-1MB) to be safe (BIOS, VGA, Kernel starts at 1MB) */
        if (mmap->addr_low < 0x100000)
            goto next;

        /* Free pages in this region */
        uint32_t start_addr = mmap->addr_low;
        uint32_t length = mmap->len_low;
        uint32_t start_page = start_addr / PMM_PAGE_SIZE;
        uint32_t num_pages = length / PMM_PAGE_SIZE;

        for (uint32_t i = 0; i < num_pages; i++) {
            /* Check if this page overlaps with our bitmap itself! */
            uint32_t page_addr = (start_page + i) * PMM_PAGE_SIZE;

            /* Check overlap with kernel code/modules/bitmap */
            if (page_addr >= meta_end_addr)
                /* The bitmap is now placed AFTER everything (kernel + modules + struct) */
                /* So we just need to check if page_addr is >= bitmap_end_addr to be safe */
                /* Actually, we should free pages that are AFTER the bitmap. */
                /* But wait, what if there is free memory BETWEEN kernel end and bitmap start? */
                /* We moved bitmap to end of everything. So everything before bitmap is used (kernel, modules). */
                /* So freeing only pages >= bitmap_end_addr is safe and correct. */
                bitmap_unset(start_page + i);
        }

next:
        mmap = (struct multiboot_memory_map*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
    }

    printf("PMM: Bitmap at 0x%x, managing %d pages (%d MB)\n", (uint32_t)pmm_bitmap, total_pages, total_memory_kb / 1024);
}