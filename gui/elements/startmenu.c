// gui/elements/startmenu.c — menu iniciar
#include "startmenu.h"
#include "taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"

void draw_start_menu(void) {
    uint32_t sh = fb_height();
    uint32_t mw = 200, mh = 240;
    uint32_t mx = 4, my = sh - TASKBAR_H - mh;

    fb_draw_shadow(mx + 6, my + 6, mw, mh);
    fb_draw_rounded_rect(mx, my, mw, mh, 0x0A0F1E, 8);
    fb_fill_rect(mx + 1, my + 1, mw - 2, 1, 0x2A4A8A);

    fb_fill_gradient_v(mx + 1, my + 1, mw - 2, 44, 0x0D2040, 0x08101A);
    fb_fill_rect(mx + 1, my + 44, mw - 2, 1, 0x0A1428);
    fb_draw_string_centered(mx, my + 1, mw, 44,
                             "HAOS  v1.1", COLOR_TEXT_LIGHT, 0, true);

    const char* items[] = {
        "  [T]  Terminal",
        "  [A]  Sobre o HAOS",
        "  [C]  Configurações",
        "",
        "  [R]  Reiniciar",
        "  [Q]  Desligar",
    };
    int item_h = 34;
    int iy = (int)my + 50;
    for (int i = 0; i < 6; i++) {
        if (!items[i][0]) {
            fb_fill_rect(mx + 12, (uint32_t)(iy + item_h/2), mw - 24, 1, 0x1A2840);
        } else {
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, item_h - 2, 0x0C1428);
            fb_draw_string(mx + 10, (uint32_t)(iy + (item_h - FONT_H)/2),
                           items[i], COLOR_TEXT_LIGHT, 0, true);
        }
        iy += item_h;
    }

    fb_draw_string_centered(mx, (uint32_t)(my + mh - 22), mw, 18,
                             "[ESC] fechar",
                             COLOR_TEXT_DIM, 0, true);
}