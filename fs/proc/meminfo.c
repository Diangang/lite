// fs/proc/meminfo.c - Lite procfs /proc/meminfo (Linux mapping: fs/proc/meminfo.c)

#include <stdint.h>

#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/fs.h"
#include "linux/file.h"
#include "linux/bootmem.h"
#include "asm/pgtable.h"
#include "linux/mmzone.h"
#include "linux/gfp.h"
#include "linux/mmzone.h"

#include "internal.h"

static void buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || cap == 0 || !s)
        return;
    while (*s && *off + 1 < cap) {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

static void buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%u", v);
    buf_append(buf, off, cap, tmp);
}

/* proc_read_meminfo: Implement proc read meminfo. */
static uint32_t proc_read_meminfo(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;

    static char tmp[1536];
    uint32_t off = 0;

    uint32_t total_kb = (uint32_t)(totalram_pages() * (PAGE_SIZE / 1024));
    uint32_t free_kb = (uint32_t)(nr_free_pages() * (PAGE_SIZE / 1024));

    uint32_t min_kb = 0;
    uint32_t low_kb = 0;
    uint32_t high_kb = 0;

    uint32_t zone_dma_total_kb = contig_page_data.zone_dma.managed_pages * (PAGE_SIZE / 1024);
    uint32_t zone_dma_free_kb = (uint32_t)(zone_free_pages(&contig_page_data.zone_dma) * (PAGE_SIZE / 1024));
    uint32_t zone_normal_total_kb = contig_page_data.zone_normal.managed_pages * (PAGE_SIZE / 1024);
    uint32_t zone_normal_free_kb = (uint32_t)(zone_free_pages(&contig_page_data.zone_normal) * (PAGE_SIZE / 1024));

    uint32_t e820_ram_kb = bootmem_ram_kb();
    uint32_t e820_reserved_kb = bootmem_reserved_kb();

    uint32_t lowmem_end_kb = bootmem_lowmem_end() / 1024;
    uint32_t lowmem_phys_end_kb = memlayout_lowmem_phys_end() / 1024;

    uint32_t direct_map_start_kb = memlayout_directmap_start() / 1024;
    uint32_t direct_map_end_kb = memlayout_directmap_end() / 1024;

    uint32_t vmalloc_start_kb = memlayout_vmalloc_start() / 1024;
    uint32_t vmalloc_end_kb = memlayout_vmalloc_end() / 1024;

    uint32_t fixaddr_start_kb = memlayout_fixaddr_start() / 1024;

    if (contig_page_data.zone_dma.spanned_pages) {
        min_kb += contig_page_data.zone_dma.watermark[WMARK_MIN] * (PAGE_SIZE / 1024);
        low_kb += contig_page_data.zone_dma.watermark[WMARK_LOW] * (PAGE_SIZE / 1024);
        high_kb += contig_page_data.zone_dma.watermark[WMARK_HIGH] * (PAGE_SIZE / 1024);
    }
    if (contig_page_data.zone_normal.spanned_pages) {
        min_kb += contig_page_data.zone_normal.watermark[WMARK_MIN] * (PAGE_SIZE / 1024);
        low_kb += contig_page_data.zone_normal.watermark[WMARK_LOW] * (PAGE_SIZE / 1024);
        high_kb += contig_page_data.zone_normal.watermark[WMARK_HIGH] * (PAGE_SIZE / 1024);
    }

    buf_append(tmp, &off, sizeof(tmp), "MemTotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nMemFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), free_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nE820Ram: ");
    buf_append_u32(tmp, &off, sizeof(tmp), e820_ram_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nE820Reserved: ");
    buf_append_u32(tmp, &off, sizeof(tmp), e820_reserved_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nLowMemEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), lowmem_end_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nLowMemPhysEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), lowmem_phys_end_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nDirectMapStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), direct_map_start_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nDirectMapEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), direct_map_end_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nVmallocStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), vmalloc_start_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nVmallocEnd: ");
    buf_append_u32(tmp, &off, sizeof(tmp), vmalloc_end_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nFixaddrStart: ");
    buf_append_u32(tmp, &off, sizeof(tmp), fixaddr_start_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nMinFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), min_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nLowFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), low_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nHighFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), high_kb);

    buf_append(tmp, &off, sizeof(tmp), " kB\nKswapdWakeups: ");
    buf_append_u32(tmp, &off, sizeof(tmp), kswapd_wakeup_count());

    buf_append(tmp, &off, sizeof(tmp), "\nZoneDMATotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_dma_total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneDMAFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_dma_free_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneNormalTotal: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_normal_total_kb);
    buf_append(tmp, &off, sizeof(tmp), " kB\nZoneNormalFree: ");
    buf_append_u32(tmp, &off, sizeof(tmp), zone_normal_free_kb);
    buf_append(tmp, &off, sizeof(tmp), "\n");

    if (off < sizeof(tmp))
        tmp[off] = 0;

    if (offset >= off)
        return 0;
    uint32_t remain = off - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct file_operations proc_meminfo_ops = {
    .read = proc_read_meminfo,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct inode proc_meminfo;

void proc_meminfo_init(void)
{
    memset(&proc_meminfo, 0, sizeof(proc_meminfo));
    proc_meminfo.flags = FS_FILE;
    proc_meminfo.i_ino = 6;
    proc_meminfo.i_size = 256;
    proc_meminfo.f_ops = &proc_meminfo_ops;
    proc_meminfo.uid = 0;
    proc_meminfo.gid = 0;
    proc_meminfo.i_mode = 0444;

    proc_register_root_child("meminfo", &proc_meminfo);
}
