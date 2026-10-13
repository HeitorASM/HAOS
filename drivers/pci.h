#pragma once
#include "../kernel/types.h"

// Enumeração PCI mínima (acesso por portas 0xCF8/0xCFC, "mecanismo 1").
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if;
} PciDevice;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);

// Acha o `index`-ésimo dispositivo com class/subclass (e prog_if, se != 0xFF).
bool pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint32_t index, PciDevice* out);

// Liga "memory space" e "bus master" (DMA) no registrador de comando.
void pci_enable_mem_dma(const PciDevice* d);

#ifdef __cplusplus
}
#endif
