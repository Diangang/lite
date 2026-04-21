#ifndef LINUX_START_KERNEL_H
#define LINUX_START_KERNEL_H

#include <linux/init.h>

/*
 * Linux mapping: linux2.6/include/linux/start_kernel.h declares start_kernel().
 *
 * Lite alignment: arch code is responsible for bootloader-specific handoff
 * (Multiboot magic/info) and must prepare any global boot state before calling
 * start_kernel().
 */
void __init start_kernel(void);

#endif
