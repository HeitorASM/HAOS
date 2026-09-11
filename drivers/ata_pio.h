#pragma once

#include "block.h"

bool         ata_pio_init(void);
bool         ata_pio_available(void);
BlockDevice* ata_pio_device(void);