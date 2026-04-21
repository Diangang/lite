#ifndef LINUX_INIT_H
#define LINUX_INIT_H

#include <stdint.h>

typedef int (*initcall_t)(void);

/*
 * Linux mapping: include/linux/init.h defines section annotations for init/exit.
 *
 * Lite simplification: we don't reclaim init sections yet, so these are kept as
 * no-ops to preserve Linux-compatible signatures without changing runtime.
 */
#define __init
#define __initdata
#define __initconst
#define __exit
#define __exitdata

#define __stringify_1(x) #x
#define __stringify(x) __stringify_1(x)

#define __define_initcall(fn, level) static initcall_t __initcall_##fn __attribute__((__used__, __section__(".initcall" __stringify(level) ".init"))) = fn;

#define early_initcall(fn) __define_initcall(fn, 0)
#define core_initcall(fn) __define_initcall(fn, 1)
#define subsys_initcall(fn) __define_initcall(fn, 2)
#define fs_initcall(fn) __define_initcall(fn, 3)
#define device_initcall(fn) __define_initcall(fn, 4)
#define late_initcall(fn) __define_initcall(fn, 5)

#define module_init(fn) device_initcall(fn)

/* initramfs.c  */
void populate_rootfs(void);

#endif
