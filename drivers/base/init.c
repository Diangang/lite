#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "base.h"

/* driver_init: Initialize driver. */
void driver_init(void)
{
    /* Linux-like: init.c only orchestrates subsystem init order. */
    devtmpfs_init();
    devices_init();
    buses_init();
    classes_init();
    platform_bus_init();
    printf("Driver core initialized.\n");
}
