#include "linux/device.h"
#include "linux/libc.h"
#include "linux/platform_device.h"
#include "base.h"

/* driver_init: Initialize driver. */
void driver_init(void)
{
    /* Linux-like: init.c only orchestrates subsystem init order. */
    device_model_kset_init();
    platform_bus_init();
    printf("Driver core initialized.\n");
}
