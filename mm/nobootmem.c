#include "linux/mmzone.h"

/*
 * Linux mapping: linux2.6/mm/nobootmem.c::contig_page_data
 *
 * Lite uses a single node/zone model. We keep the global pglist_data instance
 * and place it under mm/nobootmem.c to match Linux placement.
 */

struct pglist_data contig_page_data;

