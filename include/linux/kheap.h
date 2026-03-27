#ifndef LINUX_KHEAP_H
#define LINUX_KHEAP_H

#include <stdint.h>
#include <stddef.h>

#define KHEAP_START 0xC0000000
#define KHEAP_INITIAL_SIZE 0x100000

struct header {
    struct header *next;
    size_t size;
    uint8_t is_free;
    uint32_t magic;
};

#define HEAP_MAGIC 0x12345678

void init_kheap(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void kheap_print_stats(void);

#endif
