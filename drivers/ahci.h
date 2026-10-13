#pragma once
#include "block.h"

// Driver AHCI (SATA nativo) — DMA, 48-bit LBA, por polling (sem IRQ).
// Suporta até AHCI_MAX_DEVICES discos SATA (ATA) com setor lógico de 512 B.
#define AHCI_MAX_DEVICES 4

#ifdef __cplusplus
extern "C" {
#endif

int          ahci_init(void);           // devolve quantos discos achou (0 = nenhum)
BlockDevice* ahci_device(int index);

#ifdef __cplusplus
}
#endif
