#include "pmm.h"
#include "libc.h"
#include "kernel.h"

static multiboot_info_t* cached_mbi = NULL;
static uint32_t total_memory_kb = 0;
static uint32_t total_pages = 0;
static uint32_t* pmm_bitmap = NULL;
static uint32_t bitmap_size = 0;

/* Bitmap Helpers */
static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_unset(uint32_t bit) {
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline int bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

/* Find first free page (first bit that is 0) */
static int bitmap_first_free(void) {
    for (uint32_t i = 0; i < bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1 << j))) {
                    return i * 32 + j;
                }
            }
        }
    }
    return -1;
}

extern uint32_t end; /* Defined in linker.ld, end of kernel */

void pmm_init(multiboot_info_t* mbi)
{
    cached_mbi = mbi;

    /* 1. Calculate total memory */
    if (mbi->flags & 1) {
        total_memory_kb = mbi->mem_lower + mbi->mem_upper;
    }

    total_pages = (total_memory_kb * 1024) / PMM_PAGE_SIZE;

    /* 2. Place bitmap after the kernel */
    /* We use the 'end' symbol from linker script to find where kernel ends */
    uint32_t kernel_end = (uint32_t)&end;

    /* Align kernel_end to page boundary */
    if (kernel_end % PMM_PAGE_SIZE) {
        kernel_end += PMM_PAGE_SIZE - (kernel_end % PMM_PAGE_SIZE);
    }

    pmm_bitmap = (uint32_t*)kernel_end;
    bitmap_size = total_pages / 32;
    if (total_pages % 32) bitmap_size++;

    /* Initialize bitmap: Mark ALL pages as used first (safe default) */
    memset(pmm_bitmap, 0xFF, bitmap_size * 4);

    /* 3. Parse memory map to free available pages */
    if (mbi->flags & (1 << 6)) {
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)mbi->mmap_addr;
        while ((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {

            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint32_t start_addr = mmap->addr_low;
                uint32_t length = mmap->len_low;

                /* Don't free low memory (0-1MB) to be safe (BIOS, VGA, Kernel stack...) */
                if (start_addr < 0x100000) {
                    /* Skip low memory */
                } else {
                    /* Free pages in this region */
                    uint32_t start_page = start_addr / PMM_PAGE_SIZE;
                    uint32_t num_pages = length / PMM_PAGE_SIZE;

                    for (uint32_t i = 0; i < num_pages; i++) {
                        /* Check if this page overlaps with our bitmap itself! */
                        uint32_t page_addr = (start_page + i) * PMM_PAGE_SIZE;
                        uint32_t bitmap_end_addr = (uint32_t)pmm_bitmap + (bitmap_size * 4);

                        /* Also check overlap with kernel code (0x100000 - &end) */
                        /* But wait, we placed bitmap AFTER &end, so we just need to protect up to bitmap_end_addr */

                        if (page_addr >= bitmap_end_addr) {
                            bitmap_unset(start_page + i);
                        }
                    }
                }
            }
            mmap = (multiboot_memory_map_t*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    }

    printf("PMM: Bitmap at 0x%x, managing %d pages (%d MB)\n", (uint32_t)pmm_bitmap, total_pages, total_memory_kb / 1024);
}

void* pmm_alloc_page(void)
{
    int frame = bitmap_first_free();
    if (frame == -1) {
        printf("PMM: Out of memory!\n");
        return NULL;
    }

    bitmap_set(frame);
    uint32_t addr = frame * PMM_PAGE_SIZE;
    return (void*)addr;
}

void pmm_free_page(void* p)
{
    uint32_t addr = (uint32_t)p;
    uint32_t frame = addr / PMM_PAGE_SIZE;
    bitmap_unset(frame);
}

void pmm_print_memory_map(void)
{
    if (!cached_mbi) {
        printf("PMM not initialized or Multiboot info not found.\n");
        return;
    }

    if (cached_mbi->flags & 1) {
        printf("Total Memory: %d KB (%d MB)\n", total_memory_kb, total_memory_kb / 1024);
    }

    if (cached_mbi->flags & (1 << 6)) {
        printf("Memory Map provided by BIOS:\n");
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)cached_mbi->mmap_addr;

        while ((uint32_t)mmap < cached_mbi->mmap_addr + cached_mbi->mmap_length) {

            const char* type_str = "Unknown";
            switch (mmap->type) {
                case MULTIBOOT_MEMORY_AVAILABLE: type_str = "Available RAM"; break;
                case MULTIBOOT_MEMORY_RESERVED:  type_str = "Reserved (Hardware)"; break;
                case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: type_str = "ACPI Reclaimable"; break;
                case MULTIBOOT_MEMORY_NVS:       type_str = "ACPI NVS"; break;
                case MULTIBOOT_MEMORY_BADRAM:    type_str = "Bad RAM"; break;
            }

            /* We only print the low 32-bits for now since it's a 32-bit OS */
            uint32_t size_kb = mmap->len_low / 1024;

            printf("  [0x%x - 0x%x] %d KB (%s)\n",
                   mmap->addr_low,
                   mmap->addr_low + mmap->len_low - 1,
                   size_kb,
                   type_str);

            /* Move to the next entry. The size field does not include the size field itself (4 bytes) */
            mmap = (multiboot_memory_map_t*) ((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    } else {
        printf("BIOS did not provide a memory map.\n");
    }
}