// drivers/font.h
#pragma once
#include "../kernel/types.h"   // usa uint8_t, etc. do projeto

#define FONT_W  8
#define FONT_H  16

// Retorna ponteiro para 16 bytes do glifo 8x16 (CP437)
#ifdef __cplusplus
extern "C" {   // font.c é C: sem isto, código C++ (ex.: gui/core/userwin.cpp) geraria
               // um símbolo com nome "mangled" e falharia no link.
#endif
const uint8_t* font_get(uint8_t c);
#ifdef __cplusplus
}
#endif