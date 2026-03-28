#include "linux/ksysfs.h"
#include "linux/sysfs.h"

void ksysfs_init(void)
{
    init_sysfs();
}
