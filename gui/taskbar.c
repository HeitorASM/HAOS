#include "taskbar.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>

#define MENU_ITEMS 4
static const char* MENU_LABELS[MENU_ITEMS] = {
    "  Terminal",
    "  About HAOS",
    "  Restart",
    "  Shutdown"
};

void taskbar_draw(void) {
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();
    int y = (int)h - TASKBAR_H;

    /* Bar background */
    fb_fill_rect(0, y, (int)w, TASKBAR_H, COL_TASKBAR);
    fb_draw_hline(0, y, (int)w, 0x00303030);

    /* Start button */
    fb_fill_rect(4, y + 4, 80, TASKBAR_H - 8, COL_ACCENT);
    fb_draw_rect(4, y + 4, 80, TASKBAR_H - 8, 0x006688CC);
    font_puts(14, y + 11, "START", COL_WHITE, COL_ACCENT);

    /* Window button - Terminal */
    fb_fill_rect(94, y + 4, 120, TASKBAR_H - 8, COL_DGRAY);
    fb_draw_rect(94, y + 4, 120, TASKBAR_H - 8, COL_GRAY);
    font_puts(102, y + 11, "> Terminal", COL_WHITE, COL_DGRAY);

    /* Clock placeholder */
    font_puts((int)w - 90, y + 11, "HAOS v0.1", COL_GRAY, COL_TASKBAR);
}

void start_menu_draw(int visible) {
    uint32_t h = fb_get_height();
    int menu_w = 200;
    int item_h = 28;
    int menu_h = MENU_ITEMS * item_h + 4;
    int mx = 4;
    int my = (int)h - TASKBAR_H - menu_h - 2;

    if (!visible) {
        /* Erase menu area by redrawing desktop background */
        fb_fill_rect(mx, my, menu_w, menu_h + 2, 0x00C0C0C0);
        return;
    }

    /* Menu background */
    fb_fill_rect(mx, my, menu_w, menu_h, COL_VDGRAY);
    fb_draw_rect(mx, my, menu_w, menu_h, COL_GRAY);

    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = my + 2 + i * item_h;
        font_puts(mx + 4, iy + 9, MENU_LABELS[i], COL_WHITE, COL_VDGRAY);
        fb_draw_hline(mx + 4, iy + item_h - 1, menu_w - 8, 0x00303030);
    }
}

int start_menu_handle(int key, int* item) {
    (void)key; (void)item;
    return 0; /* mouse-less: handled externally */
}
