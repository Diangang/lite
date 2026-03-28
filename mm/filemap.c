#include "linux/fs.h"
#include "linux/page_alloc.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/pagemap.h"
#include "asm/page.h"

void address_space_init(struct address_space *mapping, struct inode *host)
{
    mapping->host = host;
    mapping->pages = NULL;
    mapping->nrpages = 0;
}

static struct page_cache_entry *find_get_page(struct address_space *mapping, uint32_t index)
{
    struct page_cache_entry *p = mapping->pages;
    while (p) {
        if (p->index == index)
            return p;
        p = p->next;
    }
    return NULL;
}

static struct page_cache_entry *add_to_page_cache(struct address_space *mapping, uint32_t index)
{
    struct page_cache_entry *p = (struct page_cache_entry *)kmalloc(sizeof(struct page_cache_entry));
    if (!p)
        return NULL;
    p->index = index;
    // Allocate a physical page for cache
    p->phys_addr = (uint32_t)alloc_page(GFP_KERNEL);
    if (!p->phys_addr) {
        kfree(p);
        return NULL;
    }

    memset(phys_to_virt(p->phys_addr), 0, 4096);

    p->next = mapping->pages;
    mapping->pages = p;
    mapping->nrpages++;
    return p;
}

uint32_t generic_file_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    if ((node->flags & 0x7) != FS_FILE)
        return 0;
    if (offset >= node->i_size)
        return 0;

    uint32_t remain = node->i_size - offset;
    if (size > remain) size = remain;

    uint32_t bytes_read = 0;
    struct address_space *mapping = node->i_mapping;
    if (!mapping)
        return 0;

    while (size > 0) {
        uint32_t index = offset / 4096;
        uint32_t page_offset = offset % 4096;
        uint32_t bytes = 4096 - page_offset;
        if (bytes > size) bytes = size;

        struct page_cache_entry *p = find_get_page(mapping, index);
        if (p)
            memcpy(buffer + bytes_read, (void*)((uint32_t)phys_to_virt(p->phys_addr) + page_offset), bytes);
        else
            // Page not in cache, in a real OS we would read from disk here.
            // Since this is generic and backing ramfs, if it's not in cache, it's just zeroes.
            memset(buffer + bytes_read, 0, bytes);

        offset += bytes;
        bytes_read += bytes;
        size -= bytes;
    }
    return bytes_read;
}

uint32_t generic_file_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    if ((node->flags & 0x7) != FS_FILE)
        return 0;

    struct address_space *mapping = node->i_mapping;
    if (!mapping)
        return 0;

    uint32_t bytes_written = 0;

    while (size > 0) {
        uint32_t index = offset / 4096;
        uint32_t page_offset = offset % 4096;
        uint32_t bytes = 4096 - page_offset;
        if (bytes > size) bytes = size;

        struct page_cache_entry *p = find_get_page(mapping, index);
        if (!p) {
            p = add_to_page_cache(mapping, index);
            if (!p)
                break; // Out of memory
        }

        memcpy((void*)((uint32_t)phys_to_virt(p->phys_addr) + page_offset), buffer + bytes_written, bytes);

        offset += bytes;
        bytes_written += bytes;
        size -= bytes;
    }

    if (offset > node->i_size)
        node->i_size = offset;

    return bytes_written;
}

void truncate_inode_pages(struct address_space *mapping, uint32_t lstart)
{
    if (!mapping)
        return;

    // For simplicity, we just free all pages if lstart is 0
    if (lstart == 0) {
        struct page_cache_entry *p = mapping->pages;
        while (p) {
            struct page_cache_entry *next = p->next;
            if (p->phys_addr)
                free_page((unsigned long)p->phys_addr);

            // Also need to remove it from the mapping list?
            // Since we are clearing the whole mapping, we can just free the structs
            kfree(p);
            p = next;
        }
        mapping->pages = NULL;
        mapping->nrpages = 0;
    }
}
