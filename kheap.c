#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "libc.h"
#include "kernel.h"

/* The start of the free list */
static header_t *free_list = NULL;

/* The end of the heap */
static uint32_t heap_end = KHEAP_START;

/* Extend the heap by mapping more pages */
static void kheap_extend(size_t size) {
    size_t pages_needed = (size + 4095) / 4096;

    for (size_t i = 0; i < pages_needed; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            printf("KHEAP: Out of physical memory!\n");
            return;
        }

        /* Map the physical page to the current end of heap */
        vmm_map_page(phys, (void*)heap_end);

        /*
         * IMPORTANT: Since we are not identity mapping the heap,
         * we cannot access 'phys' directly to clear it.
         * We MUST access it through the virtual address 'heap_end'.
         */
        memset((void*)heap_end, 0, 4096);

        heap_end += 4096;
    }
}

void kheap_init(void) {
    /* Map initial 1MB for the heap */
    kheap_extend(KHEAP_INITIAL_SIZE);

    /* Create the first free block covering the entire initial heap */
    free_list = (header_t*)KHEAP_START;
    free_list->size = KHEAP_INITIAL_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;
    free_list->magic = HEAP_MAGIC;

    printf("KHEAP: Initialized at 0x%x with %d bytes\n", KHEAP_START, KHEAP_INITIAL_SIZE);
}

void *kmalloc(size_t size) {
    /* Add header size and align to 8 bytes */
    size_t total_size = size + sizeof(header_t);
    if (total_size % 8 != 0) {
        total_size += 8 - (total_size % 8);
    }

    header_t *curr = free_list;

    /* First Fit: Find the first free block that fits */
    while (curr) {
        if (curr->is_free && curr->size >= total_size) {
            /* Found a suitable block */

            /* Should we split it? */
            if (curr->size >= total_size + sizeof(header_t) + 8) {
                /* Yes, split into [Used] + [New Free] */
                header_t *new_free = (header_t*)((uint32_t)curr + total_size);

                new_free->size = curr->size - total_size;
                new_free->is_free = 1;
                new_free->next = curr->next;
                new_free->magic = HEAP_MAGIC;

                curr->size = total_size;
                curr->next = new_free;
            }

            curr->is_free = 0;
            return (void*)((uint32_t)curr + sizeof(header_t));
        }
        curr = curr->next;
    }

    /* No free block found! Should extend heap here... */
    printf("KHEAP: Out of memory! (TODO: Extend heap)\n");
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* Get the header */
    header_t *header = (header_t*)((uint32_t)ptr - sizeof(header_t));

    /* Sanity check */
    if (header->magic != HEAP_MAGIC) {
        printf("KHEAP: Double free or corruption detected at 0x%x!\n", ptr);
        return;
    }

    header->is_free = 1;

    /* Coalesce: Merge with next block if free */
    if (header->next && header->next->is_free) {
        header->size += header->next->size;
        header->next = header->next->next;
    }

    /* Coalesce: We should also merge with prev, but single linked list makes it O(N) */
    /* For a simple kernel, this is acceptable for now. */
}

void kheap_print_stats(void) {
    header_t *curr = free_list;
    int blocks = 0;
    int free_blocks = 0;
    size_t total_free = 0;

    printf("--- Heap Stats ---\n");
    while (curr) {
        printf("Block %d: Addr=0x%x Size=%d Free=%d\n", blocks, curr, curr->size, curr->is_free);
        if (curr->is_free) {
            free_blocks++;
            total_free += curr->size;
        }
        blocks++;
        curr = curr->next;
    }
    printf("Total Blocks: %d, Free Blocks: %d, Total Free Bytes: %d\n", blocks, free_blocks, total_free);
}