#ifndef LINUX_KDEV_T_H
#define LINUX_KDEV_T_H

#include <stdint.h>

typedef uint32_t dev_t;

#define MINORBITS 20
#define MINORMASK ((1U << MINORBITS) - 1)

#define MAJOR(dev) ((uint32_t)((dev) >> MINORBITS))
#define MINOR(dev) ((uint32_t)((dev) & MINORMASK))
#define MKDEV(ma, mi) ((((dev_t)(ma)) << MINORBITS) | ((dev_t)(mi) & MINORMASK))

#endif
