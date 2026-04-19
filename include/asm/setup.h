#ifndef ASM_SETUP_H
#define ASM_SETUP_H

#include "asm/multiboot.h"

void setup_arch(struct multiboot_info* mbi);
void i386_start_kernel(uint32_t magic, struct multiboot_info* mbi);

#endif
