// drivers/font.h
#pragma once
#include "../kernel/types.h"   // usa uint8_t, etc. do projeto

#define FONT_W  8
#define FONT_H  16

// Retorna ponteiro para 16 bytes do glifo 8x16 (CP437)
const uint8_t* font_get(uint8_t c);