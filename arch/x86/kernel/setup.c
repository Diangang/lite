#include "linux/device_model.h"
#include "linux/init.h"
#include "linux/libc.h"

/*
 * In Linux, architecture-specific code (like arch/x86/kernel/setup.c or board-*.c)
 * is responsible for telling the core kernel what hardware devices exist on the board
 * by registering platform devices.
 */
static int x86_platform_devices_init(void)
{
    struct bus_type *platform = device_model_platform_bus();
    if (!platform)
        return -1;

    // Register static hardware devices that exist on this "board"
    device_register_simple("console", "console", platform, NULL);
    device_register_simple("ramfs", "memfs", platform, NULL);

    return 0;
}
// We use module_init so this runs automatically during do_initcalls()
module_init(x86_platform_devices_init);
