// gui/apps/terminal.h
#pragma once
#include "../window.h"

#define TERM_COLS     80
#define TERM_ROWS     24
#define TERM_BUF_SIZE 256
#define TERM_HIST     100

Window* terminal_create(int32_t x, int32_t y);
void terminal_tick(Window* win, uint64_t ticks);