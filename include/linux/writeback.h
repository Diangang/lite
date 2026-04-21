#ifndef LINUX_WRITEBACK_H
#define LINUX_WRITEBACK_H

#include <stdint.h>

struct address_space;

/*
 * Linux mapping: writeback control belongs to include/linux/writeback.h.
 * Lite keeps a synchronous subset but exposes a dedicated writeback layer.
 */

void balance_dirty_pages_ratelimited(struct address_space *mapping);
int writeback_flush_all(void);
void get_writeback_stats(uint32_t *dirty, uint32_t *cleaned,
                         uint32_t *discarded, uint32_t *throttled);

/* Lite writeback accounting helpers used by the page-cache subset. */
void writeback_account_dirtied(void);
void writeback_account_cleaned(void);
void writeback_account_discarded(void);

#endif
