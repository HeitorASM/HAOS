#include "gui.h"
#include "window.h"
#include "screens/boot.h"
#include "screens/welcome.h"
#include "screens/desktop.h"
#include "../kernel/keyboard.h"   
#include "../drivers/mouse.h"
#include "../kernel/types.h"

extern volatile uint64_t timer_ticks;

static GuiState state = GUI_STATE_BOOT;

void gui_init(void) {
    wm_init();
    state = GUI_STATE_BOOT;
}

void gui_run(void) {
    run_boot_screen();

    state = GUI_STATE_WELCOME;
    draw_welcome_screen();

    // Aguarda ENTER ou timeout de 5 segundos
    uint64_t start = timer_ticks;
    while (1) {
        mouse_process();
        uint8_t c = keyboard_getchar();
        if (c == '\n' || c == '\r' || c == ' ') break;
        if (timer_ticks - start > 500) break;
        __asm__("pause");
    }

    state = GUI_STATE_DESKTOP;
    run_desktop();
}