#include "startmenu.h"
#include "taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"
#include "../../kernel/memory.h"

void draw_start_menu(void) {
    uint32_t sh = fb_height();
    uint32_t mw = 210, mh = 244;
    uint32_t mx = 4, my = sh - TASKBAR_H - mh;

    fb_draw_shadow(mx + 6, my + 6, mw, mh);
    fb_draw_rounded_rect(mx, my, mw, mh, 0x0C1220, 9);
    fb_fill_rect(mx + 1, my + 1, mw - 2, 1, 0x3A5AA0);

    fb_fill_gradient_v(mx + 1, my + 1, mw - 2, 44, 0x0F2648, 0x0A1220);
    fb_fill_rect(mx + 1, my + 44, mw - 2, 1, 0x223A66);
    fb_draw_string_centered(mx, my + 1, mw, 44,
                             tr(STR_STARTMENU_HEADER), COLOR_TEXT_LIGHT, 0, true);

    char item_terminal[40], item_about[40], item_settings[40];
    kstrcpy(item_terminal, "  [T]  "); kstrcat(item_terminal, tr(STR_TERMINAL));
    kstrcpy(item_about,    "  [A]  "); kstrcat(item_about,    tr(STR_ABOUT));
    kstrcpy(item_settings, "  [C]  "); kstrcat(item_settings, tr(STR_SETTINGS));
    char item_restart[40], item_shutdown[40];
    kstrcpy(item_restart,  "  [R]  "); kstrcat(item_restart,  tr(STR_RESTART));
    kstrcpy(item_shutdown, "  [Q]  "); kstrcat(item_shutdown, tr(STR_SHUTDOWN));

    const char* items[] = {
        item_terminal,
        item_about,
        item_settings,
        "",
        item_restart,
        item_shutdown,
    };
    int item_h = 34;
    int iy = (int)my + 50;
    for (int i = 0; i < 6; i++) {
        if (!items[i][0]) {
            fb_fill_rect(mx + 12, (uint32_t)(iy + item_h/2), mw - 24, 1, 0x223A66);
        } else {
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, item_h - 2, 0x0E1830);
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, 1, 0x1E3560);
            fb_draw_string(mx + 10, (uint32_t)(iy + (item_h - FONT_H)/2),
                           items[i], COLOR_TEXT_LIGHT, 0, true);
        }
        iy += item_h;
    }

    fb_draw_string_centered(mx, (uint32_t)(my + mh - 22), mw, 18,
                             tr(STR_MENU_CLOSE_HINT),
                             COLOR_TEXT_DIM, 0, true);
}