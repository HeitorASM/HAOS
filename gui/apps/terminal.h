// gui/apps/terminal.h
#pragma once
#include "../core/window.h"

#define TERM_COLS     80
#define TERM_ROWS     24
#define TERM_BUF_SIZE 256
// Historico amplo: a janela continua mostrando TERM_ROWS linhas, mas a
// barra representa todo este buffer e diminui conforme a saida cresce.
#define TERM_HIST     8192

Window* terminal_create(int32_t x, int32_t y);
void terminal_tick(Window* win, uint64_t ticks);
