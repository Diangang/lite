#include "linux/fs.h"
#include "linux/buffer_head.h"
#include "linux/pagemap.h"
#include "linux/writeback.h"

static uint32_t wb_dirty_pages = 0;
static uint32_t wb_cleaned_pages = 0;
static uint32_t wb_discarded_pages = 0;
static uint32_t wb_throttled = 0;

/*
 * Linux mapping: balance_dirty_pages_ratelimited() belongs to page-writeback.c.
 * Lite keeps a deterministic synchronous subset with a small global limit.
 */
#define WB_DIRTY_LIMIT 64u

void writeback_account_dirtied(void)
{
    wb_dirty_pages++;
}

void writeback_account_discarded(void)
{
    if (wb_dirty_pages)
        wb_dirty_pages--;
    wb_discarded_pages++;
}

void writeback_account_cleaned(void)
{
    if (wb_dirty_pages)
        wb_dirty_pages--;
    wb_cleaned_pages++;
}

void balance_dirty_pages_ratelimited(struct address_space *mapping)
{
    (void)mapping;
    if (wb_dirty_pages > WB_DIRTY_LIMIT) {
        wb_throttled++;
        writeback_flush_all();
    }
}

int writeback_flush_all(void)
{
    int flushed = 0;
    struct address_space *m = mapping_list;
    while (m) {
        struct list_head *pos, *n;
        list_for_each_safe(pos, n, &m->dirty_pages) {
            struct page_cache_entry *p = list_entry(pos, struct page_cache_entry, lru);
            if (p->dirty) {
                struct inode *host = m->host;
                if (host && m->a_ops && m->a_ops->writepage)
                    m->a_ops->writepage(host, p);
                p->dirty = 0;
                writeback_account_cleaned();
                flushed++;
            }
            list_del(&p->lru);
            list_add_tail(&p->lru, &m->clean_pages);
            if (m->nrpages_dirty)
                m->nrpages_dirty--;
            m->nrpages_clean++;
        }
        m = m->next;
    }
    flushed += sync_dirty_buffers_all();
    return flushed;
}

void get_writeback_stats(uint32_t *dirty, uint32_t *cleaned,
                         uint32_t *discarded, uint32_t *throttled)
{
    if (dirty)
        *dirty = wb_dirty_pages;
    if (cleaned)
        *cleaned = wb_cleaned_pages;
    if (discarded)
        *discarded = wb_discarded_pages;
    if (throttled)
        *throttled = wb_throttled;
}
