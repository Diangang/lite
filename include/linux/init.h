#ifndef LINUX_INIT_H
#define LINUX_INIT_H

typedef int (*initcall_t)(void);

#define __define_initcall(fn) static initcall_t __initcall_##fn __attribute__((__used__, __section__(".initcall.init"))) = fn;

#define module_init(x) __define_initcall(x)

#endif
