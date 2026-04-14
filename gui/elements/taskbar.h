#pragma once
#include "../../kernel/types.h"   
#include <stdbool.h>              

#define TASKBAR_H    40
#define START_BTN_W  100
#define CLOCK_W      72

void draw_taskbar(uint64_t ticks, bool start_menu_open, bool terminal_active);