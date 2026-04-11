#include "linux/fs.h"
#include "linux/page_alloc.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/pagemap.h"
#include "linux/memlayout.h"
#include "asm/page.h"

static struct address_space *mapping_list = NULL;
static uint32_t wb_dirty_pages = 0;
static uint32_t wb_cleaned_pages = 0;
static uint32_t wb_discarded_pages = 0;
static uint32_t pcache_hits = 0;
static uint32_t pcache_misses = 0;

/* address_space_init: Initialize address space. */
void address_space_init(struct address_space *mapping, struct inode *host)
{
    mapping->host = host;
    mapping->a_ops = NULL;
    mapping->pages = NULL;
    mapping->nrpages = 0;
    mapping->next = mapping_list;
    mapping_list = mapping;
}

/* address_space_release: Implement address space release. */
void address_space_release(struct address_space *mapping)
{
    if (!mapping)
        return;
    if (mapping_list == mapping) {
        mapping_list = mapping->next;
        return;
    }
    struct address_space *prev = mapping_list;
    while (prev && prev->next) {
        if (prev->next == mapping) {
            prev->next = mapping->next;
            return;
        }
        prev = prev->next;
    }
}

/* find_get_page: Find get page. */
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

/* add_to_page_cache: Implement add to page cache. */
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
    p->dirty = 0;

    memset(memlayout_directmap_phys_to_virt(p->phys_addr), 0, 4096);

    p->next = mapping->pages;
    mapping->pages = p;
    mapping->nrpages++;
    return p;
}

/* generic_file_read: Implement generic file read. */
uint32_t generic_file_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    uint32_t bytes_read = 0;
    struct address_space *mapping = node->i_mapping;
    if (!mapping)
        return 0;

    uint32_t isize = node->i_size;
    if (offset >= isize)
        return 0;

    uint32_t remain = isize - offset;
    if (size > remain) size = remain;

    while (size > 0) {
        uint32_t index = offset / 4096;
        uint32_t page_offset = offset % 4096;
        uint32_t bytes = 4096 - page_offset;
        if (bytes > size) bytes = size;

        struct page_cache_entry *p = find_get_page(mapping, index);
        if (p) {
            pcache_hits++;
            memcpy(buffer + bytes_read, (void*)((uint32_t)memlayout_directmap_phys_to_virt(p->phys_addr) + page_offset), bytes);
        } else {
            pcache_misses++;
            if (mapping->a_ops && mapping->a_ops->readpage) {
                p = add_to_page_cache(mapping, index);
                if (!p) {
                    memset(buffer + bytes_read, 0, bytes);
                } else {
                    if (mapping->a_ops->readpage(node, index, p) != 0)
                        memset(memlayout_directmap_phys_to_virt(p->phys_addr), 0, 4096);
                    memcpy(buffer + bytes_read, (void*)((uint32_t)memlayout_directmap_phys_to_virt(p->phys_addr) + page_offset), bytes);
                }
            } else {
                memset(buffer + bytes_read, 0, bytes);
            }
        }

        offset += bytes;
        bytes_read += bytes;
        size -= bytes;
    }
    return bytes_read;
}

/* generic_file_write: Implement generic file write. */
uint32_t generic_file_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer)
{
    if (!node || !buffer || size == 0)
        return 0;
    struct address_space *mapping = node->i_mapping;
    if (!mapping)
        return 0;
    if (mapping->a_ops && mapping->a_ops->writepage) {
        if (offset >= node->i_size)
            return 0;
        if (offset + size > node->i_size)
            size = node->i_size - offset;
    }

    uint32_t bytes_written = 0;

    while (size > 0) {
        uint32_t index = offset / 4096;
        uint32_t page_offset = offset % 4096;
        uint32_t bytes = 4096 - page_offset;
        if (bytes > size) bytes = size;

        struct page_cache_entry *p = find_get_page(mapping, index);
        if (p) {
            pcache_hits++;
        } else {
            pcache_misses++;
            p = add_to_page_cache(mapping, index);
            if (!p)
                break;
            if (mapping->a_ops && mapping->a_ops->readpage && (page_offset != 0 || bytes < 4096)) {
                if (mapping->a_ops->readpage(node, index, p) != 0)
                    memset(memlayout_directmap_phys_to_virt(p->phys_addr), 0, 4096);
            }
        }

        memcpy((void*)((uint32_t)memlayout_directmap_phys_to_virt(p->phys_addr) + page_offset), buffer + bytes_written, bytes);
        if (!p->dirty) {
            p->dirty = 1;
            wb_dirty_pages++;
        }

        offset += bytes;
        bytes_written += bytes;
        size -= bytes;
    }

    if ((!mapping->a_ops || !mapping->a_ops->writepage) && offset > node->i_size)
        node->i_size = offset;

    return bytes_written;
}

/* truncate_inode_pages: Implement truncate inode pages. */
void truncate_inode_pages(struct address_space *mapping, uint32_t lstart)
{
    if (!mapping)
        return;

    // For simplicity, we just free all pages if lstart is 0
    if (lstart == 0) {
        struct page_cache_entry *p = mapping->pages;
        while (p) {
            struct page_cache_entry *next = p->next;
            if (p->dirty) {
                p->dirty = 0;
                if (wb_dirty_pages)
                    wb_dirty_pages--;
                wb_discarded_pages++;
            }
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

/* page_cache_reclaim_one: Implement page cache reclaim one. */
int page_cache_reclaim_one(void)
{
    struct address_space *m = mapping_list;
    while (m) {
        if (m->pages) {
            struct page_cache_entry *p = m->pages;
            struct page_cache_entry *prev = NULL;
            while (p) {
                if (!p->dirty) {
                    if (prev)
                        prev->next = p->next;
                    else
                        m->pages = p->next;
                    if (m->nrpages)
                        m->nrpages--;
                    if (p->phys_addr)
                        free_page((unsigned long)p->phys_addr);
                    kfree(p);
                    return 1;
                }
                prev = p;
                p = p->next;
            }
        }
        m = m->next;
    }
    return 0;
}

/* writeback_flush_all: Implement writeback flush all. */
int writeback_flush_all(void)
{
    int flushed = 0;
    struct address_space *m = mapping_list;
    while (m) {
        struct page_cache_entry *p = m->pages;
        while (p) {
            if (p->dirty) {
                struct inode *host = m->host;
                if (host && m->a_ops && m->a_ops->writepage)
                    m->a_ops->writepage(host, p);
                p->dirty = 0;
                if (wb_dirty_pages)
                    wb_dirty_pages--;
                wb_cleaned_pages++;
                flushed++;
            }
            p = p->next;
        }
        m = m->next;
    }
    return flushed;
}

/* get_writeback_stats: Get writeback stats. */
void get_writeback_stats(uint32_t *dirty, uint32_t *cleaned, uint32_t *discarded)
{
    if (dirty)
        *dirty = wb_dirty_pages;
    if (cleaned)
        *cleaned = wb_cleaned_pages;
    if (discarded)
        *discarded = wb_discarded_pages;
}

/* get_pagecache_stats: Get page cache stats. */
void get_pagecache_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits)
        *hits = pcache_hits;
    if (misses)
        *misses = pcache_misses;
}
