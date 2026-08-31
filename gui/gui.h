#pragma once
#include "../kernel/types.h"

typedef enum {
    GUI_STATE_BOOT    = 0,
    GUI_STATE_WELCOME = 1,
    GUI_STATE_DESKTOP = 2,
} GuiState;

#ifdef __cplusplus
extern "C" {
#endif

void gui_init(void);
void gui_run(void);      // loop principal 

#ifdef __cplusplus
}
#endif