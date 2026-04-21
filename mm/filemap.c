#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/slab.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/pagemap.h"
#include "asm/pgtable.h"
#include "asm/page.h"

struct address_space *mapping_list = NULL;
static uint32_t pcache_hits = 0;
static uint32_t pcache_misses = 0;

/* address_space_init: Initialize address space. */
void address_space_init(struct address_space *mapping, struct inode *host)
{
    mapping->host = host;
    mapping->a_ops = NULL;
    mapping->nrpages = 0;
    mapping->nrpages_clean = 0;
    mapping->nrpages_dirty = 0;
    INIT_LIST_HEAD(&mapping->clean_pages);
    INIT_LIST_HEAD(&mapping->dirty_pages);
    for (uint32_t i = 0; i < PAGECACHE_HASH_SIZE; i++)
        mapping->page_hash[i] = NULL;
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
    uint32_t b = index & (PAGECACHE_HASH_SIZE - 1);
    struct page_cache_entry *p = mapping->page_hash[b];
    while (p) {
        if (p->index == index)
            return p;
        p = p->hash_next;
    }
    return NULL;
}

static void page_cache_hash_add(struct address_space *mapping, struct page_cache_entry *p)
{
    uint32_t b = p->index & (PAGECACHE_HASH_SIZE - 1);
    p->hash_next = mapping->page_hash[b];
    mapping->page_hash[b] = p;
}

static void page_cache_hash_del(struct address_space *mapping, struct page_cache_entry *p)
{
    uint32_t b = p->index & (PAGECACHE_HASH_SIZE - 1);
    struct page_cache_entry **pp = &mapping->page_hash[b];
    while (*pp) {
        if (*pp == p) {
            *pp = p->hash_next;
            p->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* add_to_page_cache: Implement add to page cache. */
static struct page_cache_entry *add_to_page_cache(struct address_space *mapping, uint32_t index)
{
    struct page_cache_entry *p = (struct page_cache_entry *)kmalloc(sizeof(struct page_cache_entry));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->index = index;
    // Allocate a physical page for cache
    p->phys_addr = (uint32_t)alloc_page(GFP_KERNEL);
    if (!p->phys_addr) {
        kfree(p);
        return NULL;
    }
    p->dirty = 0;

    memset(memlayout_directmap_phys_to_virt(p->phys_addr), 0, 4096);

    mapping->nrpages++;
    mapping->nrpages_clean++;
    INIT_LIST_HEAD(&p->lru);
    list_add_tail(&p->lru, &mapping->clean_pages);
    page_cache_hash_add(mapping, p);
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
            writeback_account_dirtied();
            if (mapping->nrpages_clean)
                mapping->nrpages_clean--;
            mapping->nrpages_dirty++;
            list_del(&p->lru);
            list_add_tail(&p->lru, &mapping->dirty_pages);
        }
        balance_dirty_pages_ratelimited(mapping);

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
        struct list_head *pos, *n;
        list_for_each_safe(pos, n, &mapping->dirty_pages) {
            struct page_cache_entry *p = list_entry(pos, struct page_cache_entry, lru);
            list_del(&p->lru);
            INIT_LIST_HEAD(&p->lru);
            if (p->dirty) {
                p->dirty = 0;
                writeback_account_discarded();
            }
            page_cache_hash_del(mapping, p);
            if (p->phys_addr)
                free_page((unsigned long)p->phys_addr);
            kfree(p);
            if (mapping->nrpages)
                mapping->nrpages--;
            if (mapping->nrpages_dirty)
                mapping->nrpages_dirty--;
        }
        list_for_each_safe(pos, n, &mapping->clean_pages) {
            struct page_cache_entry *p = list_entry(pos, struct page_cache_entry, lru);
            list_del(&p->lru);
            INIT_LIST_HEAD(&p->lru);
            page_cache_hash_del(mapping, p);
            if (p->phys_addr)
                free_page((unsigned long)p->phys_addr);
            kfree(p);
            if (mapping->nrpages)
                mapping->nrpages--;
            if (mapping->nrpages_clean)
                mapping->nrpages_clean--;
        }
    }
}

/* page_cache_reclaim_one: Implement page cache reclaim one. */
int page_cache_reclaim_one(void)
{
    struct address_space *m = mapping_list;
    while (m) {
        if (m->nrpages_clean && !list_empty(&m->clean_pages)) {
            struct page_cache_entry *p = list_first_entry(&m->clean_pages, struct page_cache_entry, lru);
            list_del(&p->lru);
            INIT_LIST_HEAD(&p->lru);
            page_cache_hash_del(m, p);
            if (m->nrpages)
                m->nrpages--;
            if (m->nrpages_clean)
                m->nrpages_clean--;
            if (p->phys_addr)
                free_page((unsigned long)p->phys_addr);
            kfree(p);
            return 1;
        }
        m = m->next;
    }
    return 0;
}

/* get_pagecache_stats: Get page cache stats. */
void get_pagecache_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits)
        *hits = pcache_hits;
    if (misses)
        *misses = pcache_misses;
}
