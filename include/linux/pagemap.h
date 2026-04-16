#ifndef LINUX_PAGEMAP_H
#define LINUX_PAGEMAP_H

#include <stdint.h>
#include "linux/list.h"

struct inode;
struct file;
struct dirent;

struct address_space_operations;

#define PAGECACHE_HASH_BITS 6
#define PAGECACHE_HASH_SIZE (1u << PAGECACHE_HASH_BITS)

struct page_cache_entry {
    uint32_t index;
    uint32_t phys_addr;
    uint32_t dirty;
    struct page_cache_entry *hash_next;
    struct list_head lru; /* clean_pages or dirty_pages */
};

struct address_space_operations {
    int (*readpage)(struct inode *inode, uint32_t index, struct page_cache_entry *page);
    int (*writepage)(struct inode *inode, struct page_cache_entry *page);
};

struct address_space {
    struct inode *host;
    struct address_space_operations *a_ops;
    uint32_t nrpages;
    uint32_t nrpages_clean;
    uint32_t nrpages_dirty;
    struct list_head clean_pages;
    struct list_head dirty_pages;
    struct page_cache_entry *page_hash[PAGECACHE_HASH_SIZE];
    struct address_space *next;
};

void address_space_init(struct address_space *mapping, struct inode *host);
void address_space_release(struct address_space *mapping);
uint32_t generic_file_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t generic_file_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
void truncate_inode_pages(struct address_space *mapping, uint32_t lstart);
int page_cache_reclaim_one(void);
int writeback_flush_all(void);
void get_writeback_stats(uint32_t *dirty, uint32_t *cleaned, uint32_t *discarded, uint32_t *throttled);
void get_pagecache_stats(uint32_t *hits, uint32_t *misses);

struct dirent *generic_readdir(struct file *file, uint32_t index);
struct inode *generic_finddir(struct inode *node, const char *name);

#endif
