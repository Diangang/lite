#include "linux/pci.h"
#include "linux/errno.h"

/*
 * Linux mapping: linux2.6/drivers/pci/pci-driver.c
 *
 * Lite keeps a minimal PCI driver core. This file exists primarily to align
 * symbol placement for:
 * - pci_bus_type
 */

extern const struct attribute_group *pci_dev_groups[];

static int pci_match_one(const struct pci_device_id *id, struct pci_dev *pdev)
{
    if (!id || !pdev)
        return 0;
    if (id->vendor != PCI_ANY_ID && id->vendor != (uint32_t)pdev->vendor)
        return 0;
    if (id->device != PCI_ANY_ID && id->device != (uint32_t)pdev->device)
        return 0;
    if (id->class_mask) {
        uint32_t cls = ((uint32_t)pdev->class << 16) | ((uint32_t)pdev->subclass << 8) | (uint32_t)pdev->prog_if;

        if ((cls & id->class_mask) != (id->class & id->class_mask))
            return 0;
    }
    return 1;
}

static int pci_bus_match(struct device *dev, struct device_driver *drv)
{
    if (!dev || !drv)
        return 0;
    struct pci_dev *pdev = pci_get_pci_dev(dev);

    if (!pdev)
        return 0;
    struct pci_driver *pdrv = to_pci_driver(drv);

    if (!pdrv || !pdrv->id_table)
        return 0;
    const struct pci_device_id *id = pdrv->id_table;

    while (id->vendor || id->device || id->class || id->class_mask) {
        if (pci_match_one(id, pdev))
            return 1;
        id++;
    }
    return 0;
}

struct bus_type pci_bus_type = {
    .name = "pci",
    .match = pci_bus_match,
    .dev_groups = pci_dev_groups,
};

static int pci_driver_probe(struct device *dev)
{
    if (!dev || !dev->driver)
        return -1;
    struct pci_dev *pdev = pci_get_pci_dev(dev);

    if (!pdev)
        return -1;
    struct pci_driver *pdrv = container_of(dev->driver, struct pci_driver, driver);

    if (!pdrv || !pdrv->probe || !pdrv->id_table)
        return 0;

    const struct pci_device_id *id = pdrv->id_table;
    while (id->vendor || id->device || id->class || id->class_mask) {
        if (pci_match_one(id, pdev))
            return pdrv->probe(pdev, id);
        id++;
    }
    return 0;
}

static void pci_driver_remove(struct device *dev)
{
    if (!dev || !dev->driver)
        return;
    struct pci_dev *pdev = pci_get_pci_dev(dev);

    if (!pdev)
        return;
    struct pci_driver *pdrv = container_of(dev->driver, struct pci_driver, driver);

    if (pdrv && pdrv->remove)
        pdrv->remove(pdev);
}

int pci_register_driver(struct pci_driver *drv)
{
    if (!drv || !drv->driver.name)
        return -1;
    if (!pci_bus_type.name || !pci_bus_type.name[0])
        return -1;
    init_driver(&drv->driver, drv->driver.name, &pci_bus_type, pci_driver_probe);
    drv->driver.remove = pci_driver_remove;
    return driver_register(&drv->driver);
}

void pci_unregister_driver(struct pci_driver *drv)
{
    if (!drv)
        return;
    driver_unregister(&drv->driver);
}

