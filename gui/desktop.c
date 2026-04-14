#include "desktop.h"
#include "taskbar.h"
#include "terminal.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/keyboard.h"
#include <stdint.h>

static int start_menu_visible = 0;

static void draw_desktop_bg(void) {
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();
    /* Subtle gradient desktop */
    for (uint32_t y = 0; y < h - TASKBAR_H; y++) {
        uint32_t c = fb_mix(0x00193A5E, 0x002A5F8A, (int)(y * 255 / h));
        fb_draw_hline(0, (int)y, (int)w, c);
    }
    /* Desktop label */
    font_puts(14, 14, "HAOS Desktop", 0x00AACCEE, 0x00000000);
    font_puts(14, 26, "Press S to toggle Start Menu  |  Type in Terminal below",
              0x00557799, 0x00000000);
}

static void full_redraw(int term_x, int term_y, int term_w, int term_h) {
    draw_desktop_bg();
    taskbar_draw();
    terminal_draw();
    if (start_menu_visible) start_menu_draw(1);
}

void desktop_run(void) {
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();

    /* Terminal window geometry */
    int tw = (int)w - 80;
    int th = (int)h - TASKBAR_H - 80;
    int tx = 40;
    int ty = 50;

    terminal_init(tx, ty, tw, th);
    full_redraw(tx, ty, tw, th);

    while (1) {
        int c = keyboard_getchar();
        if (c < 0) continue;

        /* S or Escape toggles start menu */
        if (c == 's' || c == 'S') {
            start_menu_visible ^= 1;
            start_menu_draw(start_menu_visible);
            continue;
        }
        if (c == 0x1B) { /* ESC */
            if (start_menu_visible) {
                start_menu_visible = 0;
                full_redraw(tx, ty, tw, th);
            }
            continue;
        }

        /* All other keys go to terminal */
        if (start_menu_visible) {
            start_menu_visible = 0;
            full_redraw(tx, ty, tw, th);
        }

        terminal_handle_key(c);
        terminal_draw();
        taskbar_draw();
    }
}
