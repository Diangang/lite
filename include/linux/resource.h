#ifndef LINUX_RESOURCE_H
#define LINUX_RESOURCE_H

#include <stdint.h>

/*
 * Linux mapping: include/linux/ioport.h / struct resource.
 * Lite only needs a minimal subset to model PCI I/O and MMIO windows.
 */

#define IORESOURCE_IO        0x00000100u
#define IORESOURCE_MEM       0x00000200u
#define IORESOURCE_PREFETCH  0x00002000u

struct resource {
    const char *name;
    uint64_t start;
    uint64_t end;
    uint32_t flags;
};

#endif
