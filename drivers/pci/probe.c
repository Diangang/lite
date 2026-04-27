#include "linux/pci.h"
#include "linux/init.h"

/*
 * Linux mapping: linux2.6/drivers/pci/probe.c
 *
 * Lite keeps only a minimal subset of the PCI probing logic in `drivers/pci/pci.c`,
 * but we align `pcibus_class` placement here.
 */

struct class pcibus_class = {
    .name = "pci_bus",
};

static int pcibus_class_init(void)
{
    return class_register(&pcibus_class);
}
postcore_initcall(pcibus_class_init);
