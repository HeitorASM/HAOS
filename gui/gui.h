#pragma once
#include "../kernel/types.h"

typedef enum {
    GUI_STATE_BOOT    = 0,
    GUI_STATE_WELCOME = 1,
    GUI_STATE_DESKTOP = 2,
} GuiState;

void gui_init(void);
void gui_run(void);      // loop principal (não retorna)
