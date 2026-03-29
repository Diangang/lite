#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include <stdint.h>
#include "linux/device.h"

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
uint32_t pci_config_read32_device(struct device *dev, uint8_t offset);
uint16_t pci_config_read16_device(struct device *dev, uint8_t offset);
uint8_t pci_config_read8_device(struct device *dev, uint8_t offset);
void pci_config_write32_device(struct device *dev, uint8_t offset, uint32_t value);

#endif
