#ifndef PAGEMAP_H
#define PAGEMAP_H

#include <stdint.h>

struct inode;

struct page_cache_entry {
    uint32_t index;            // Page index (offset / 4096)
    uint32_t phys_addr;        // Physical address of the page
    struct page_cache_entry *next; // Simple linked list for now
};

struct address_space {
    struct inode *host;
    struct page_cache_entry *pages;
    uint32_t nrpages;
};

void address_space_init(struct address_space *mapping, struct inode *host);
uint32_t generic_file_read(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t generic_file_write(struct inode *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
void truncate_inode_pages(struct address_space *mapping, uint32_t lstart);

struct file;
struct dirent *generic_readdir(struct file *file, uint32_t index);
struct inode *generic_finddir(struct inode *node, const char *name);

#endif
