#ifndef LITE_INIT_H
#define LITE_INIT_H

typedef int (*initcall_t)(void);

/*
 * The module_init macro puts the initialization function pointer
 * into the .initcall.init section, just like Linux 2.6 does.
 */
#define __define_initcall(fn)\
    static initcall_t __initcall_##fn __attribute__((__used__))\
    __attribute__((__section__(".initcall.init"))) = fn;

#define module_init(x) __define_initcall(x)

#endif
