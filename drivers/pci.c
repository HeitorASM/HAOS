#include "pci.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static inline void pci_outl(uint16_t port, uint32_t v) {
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t pci_inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 31) << 11) |
           ((uint32_t)(fn & 7) << 8) | (uint32_t)(off & 0xFC);
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    pci_outl(PCI_ADDR, cfg_addr(bus, dev, fn, off));
    return pci_inl(PCI_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    pci_outl(PCI_ADDR, cfg_addr(bus, dev, fn, off));
    pci_outl(PCI_DATA, v);
}

bool pci_find_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                    uint32_t index, PciDevice* out) {
    if (!out) return false;
    uint32_t seen = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            uint32_t id0 = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x00);
            if ((id0 & 0xFFFFu) == 0xFFFFu) continue;           // slot vazio
            uint32_t hdr = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            uint32_t nfn = ((hdr >> 16) & 0x80u) ? 8 : 1;       // multifunção?
            for (uint32_t fn = 0; fn < nfn; fn++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00);
                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);
                uint8_t cc = (uint8_t)(cls >> 24), sc = (uint8_t)(cls >> 16),
                        pi = (uint8_t)(cls >> 8);
                if (cc != class_code || sc != subclass) continue;
                if (prog_if != 0xFF && pi != prog_if) continue;
                if (seen++ != index) continue;
                out->bus = (uint8_t)bus; out->dev = (uint8_t)dev; out->fn = (uint8_t)fn;
                out->vendor = (uint16_t)(id & 0xFFFFu);
                out->device = (uint16_t)(id >> 16);
                out->class_code = cc; out->subclass = sc; out->prog_if = pi;
                return true;
            }
        }
    }
    return false;
}

void pci_enable_mem_dma(const PciDevice* d) {
    uint32_t v = pci_read32(d->bus, d->dev, d->fn, 0x04);
    // Os 16 bits altos são STATUS (write-1-to-clear): escrevemos 0 neles.
    v = (v & 0xFFFFu) | 0x6u;           // bit1 = memory space, bit2 = bus master
    pci_write32(d->bus, d->dev, d->fn, 0x04, v);
}
