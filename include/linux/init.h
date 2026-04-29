#ifndef LINUX_INIT_H
#define LINUX_INIT_H

#include <stdint.h>
#include <stddef.h>

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

/*
 * Linux mapping: initcalls are grouped into separate linker subsections.
 * Lite does not reclaim init sections yet, but we keep Linux-shaped initcall
 * levels to preserve ordering and call-site intent.
 */
#define __define_initcall(fn, id)                                           \
    static initcall_t __initcall_##fn##id                                   \
        __attribute__((__used__, __section__(".initcall" __stringify(id) ".init"))) = fn

/* Linux-shaped initcall levels (init/main.c expects 0..7 + early). */
#define early_initcall(fn) __define_initcall(fn, early)
#define pure_initcall(fn) __define_initcall(fn, 0)
#define core_initcall(fn) __define_initcall(fn, 1)
#define postcore_initcall(fn) __define_initcall(fn, 2)
#define arch_initcall(fn) __define_initcall(fn, 3)
#define subsys_initcall(fn) __define_initcall(fn, 4)
#define fs_initcall(fn) __define_initcall(fn, 5)
#define device_initcall(fn) __define_initcall(fn, 6)
#define late_initcall(fn) __define_initcall(fn, 7)

/* Backwards compatibility: prefer device_initcall() explicitly. */
#define __initcall(fn) device_initcall(fn)
#define module_init(fn) __initcall(fn)

/* initramfs.c  */
void populate_rootfs(void);

extern char *saved_command_line;
int do_one_initcall(initcall_t fn);
void setup_command_line(const char *cmdline);
const char *get_execute_command(void);
int get_cmdline_param(const char *key, char *value, size_t cap);

#endif
