#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

/* Standard Kernel Heap Start Address (Arbitrary, but safe) */
#define KHEAP_START 0xC0000000
/* Initial Size: 1MB */
#define KHEAP_INITIAL_SIZE 0x100000

/* Heap Block Header */
struct header {
    struct header *next;   /* Next free block in the free list */
    size_t size;           /* Size of the block (including header) */
    uint8_t is_free;       /* 1 = free, 0 = used */
    uint32_t magic;        /* Magic number to detect corruption */
};

#define HEAP_MAGIC 0x12345678

void kheap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void kheap_print_stats(void);

#endif