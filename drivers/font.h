#pragma once
#include "../kernel/types.h"

#define FONT_W 8
#define FONT_H 16

// Retorna ponteiro para os 16 bytes do glifo do caractere ASCII c
const uint8_t* font_get(uint8_t c);
