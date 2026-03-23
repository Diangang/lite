#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "libc.h"

/* The start of the free list */
static struct header *free_list = NULL;

/* The end of the heap */
static uint32_t heap_end = KHEAP_START;

/* Extend the heap by mapping more pages */
static int kheap_extend(size_t size) {
    size_t pages_needed = (size + 4095) / 4096;
    uint32_t old_heap_end = heap_end;
    uint32_t bytes_added = 0;

    for (size_t i = 0; i < pages_needed; i++) {
        void *phys = pmm_alloc_page();
        if (!phys)
            return printf("KHEAP: Out of physical memory!\n"), 0;

        /* Map the physical page to the current end of heap */
        vmm_map_page(phys, (void*)heap_end);

        /*
         * IMPORTANT: Since we are not identity mapping the heap,
         * we cannot access 'phys' directly to clear it.
         * We MUST access it through the virtual address 'heap_end'.
         */
        memset((void*)heap_end, 0, 4096);

        heap_end += 4096;
        bytes_added += 4096;
    }

    struct header *new_block = (struct header*)old_heap_end;
    new_block->size = bytes_added;
    new_block->is_free = 1;
    new_block->next = NULL;
    new_block->magic = HEAP_MAGIC;

    if (!free_list) {
        free_list = new_block;
        return 1;
    }

    struct header *tail = free_list;
    while (tail->next)
        tail = tail->next;

    if (tail->is_free && ((uint32_t)tail + tail->size == (uint32_t)new_block)) {
        tail->size += new_block->size;
        return 1;
    }

    tail->next = new_block;
    return 1;
}

void *kmalloc(size_t size) {
    /* Add header size and align to 8 bytes */
    size_t total_size = size + sizeof(struct header);
    if (total_size % 8 != 0)
        total_size += 8 - (total_size % 8);

    for (;;) {
        struct header *curr = free_list;

        /* First Fit: Find the first free block that fits */
        while (curr) {
            if (curr->is_free && curr->size >= total_size) {
                /* Found a suitable block */

                /* Should we split it? */
                if (curr->size >= total_size + sizeof(struct header) + 8) {
                    /* Yes, split into [Used] + [New Free] */
                    struct header *new_free = (struct header*)((uint32_t)curr + total_size);

                    new_free->size = curr->size - total_size;
                    new_free->is_free = 1;
                    new_free->next = curr->next;
                    new_free->magic = HEAP_MAGIC;

                    curr->size = total_size;
                    curr->next = new_free;
                }

                curr->is_free = 0;
                return (void*)((uint32_t)curr + sizeof(struct header));
            }
            curr = curr->next;
        }

        if (!kheap_extend(total_size))
            return printf("KHEAP: Out of memory!\n"), NULL;
    }
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    /* Get the header */
    struct header *header = (struct header*)((uint32_t)ptr - sizeof(struct header));

    /* Sanity check */
    if (header->magic != HEAP_MAGIC)
        return (void)printf("KHEAP: Double free or corruption detected at 0x%x!\n", ptr);

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
    struct header *curr = free_list;
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

void init_kheap(void) {
    /* Map initial 1MB for the heap */
    if (!kheap_extend(KHEAP_INITIAL_SIZE))
        panic("KHEAP PANIC: Failed to initialize heap.");

    /* Create the first free block covering the entire initial heap */
    free_list = (struct header*)KHEAP_START;
    free_list->size = KHEAP_INITIAL_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;
    free_list->magic = HEAP_MAGIC;

    printf("KHEAP: Initialized at 0x%x with %d bytes\n", KHEAP_START, KHEAP_INITIAL_SIZE);
}
