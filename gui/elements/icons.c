#include "icons.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"

void draw_icon_terminal(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x0F1D2C, 7);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x3A5A80);
    fb_fill_gradient_h(ix + 2, iy + 2, ICON_W - 4, 10, 0x22456E, 0x0F1D2C);

    fb_fill_circle(ix + 8,  iy + 7, 3, 0xE05555);
    fb_fill_circle(ix + 16, iy + 7, 3, 0xE0B840);
    fb_fill_circle(ix + 24, iy + 7, 3, 0x50B050);

    fb_fill_rect(ix + 2, iy + 13, ICON_W - 4, ICON_H - 15, COLOR_TERM_BG);
    fb_draw_string(ix + 5, iy + 16, ">", COLOR_TERM_PROMPT, 0, true);
    fb_draw_string(ix + 13, iy + 16, "_", COLOR_TERM_FG, 0, true);
    fb_draw_string(ix + 5, iy + 26, "$ haos", 0x5586BB, 0, true);
    fb_draw_string(ix + 5, iy + 36, "~$", 0x3F8A73, 0, true);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, tr(STR_ICON_TERMINAL),
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_icon_about(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x102040, 7);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x3A5C94);
    fb_fill_gradient_v(ix + 2, iy + 2, ICON_W - 4, ICON_H - 4, 0x102040, 0x0A1428);

    uint32_t cx = ix + ICON_W/2;
    fb_fill_circle(cx, iy + 14, 5, COLOR_ACCENT);
    fb_fill_circle(cx, iy + 14, 3, 0x102040);
    fb_fill_circle(cx, iy + 14, 2, COLOR_ACCENT);
    fb_fill_rect(cx - 3, iy + 22, 6, 16, COLOR_ACCENT);
    fb_fill_rect(cx - 5, iy + 35, 10, 4, COLOR_ACCENT);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, tr(STR_ICON_ABOUT),
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_icon_settings(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x141A2A, 7);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x3A445E);

    uint32_t cx = ix + ICON_W/2, cy = iy + ICON_H/2;
    fb_fill_circle(cx, cy, 16, 0x4A72A0);
    fb_fill_circle(cx, cy, 12, 0x141A2A);
    fb_fill_circle(cx, cy, 6, 0x4A72A0);
    fb_fill_circle(cx, cy, 3, 0x141A2A);

    fb_fill_rect(cx - 2, iy + 4, 4, 6, 0x4A72A0);
    fb_fill_rect(cx - 2, iy + ICON_H - 10, 4, 6, 0x4A72A0);
    fb_fill_rect(ix + 4, cy - 2, 6, 4, 0x4A72A0);
    fb_fill_rect(ix + ICON_W - 10, cy - 2, 6, 4, 0x4A72A0);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, tr(STR_ICON_SETTINGS),
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_icon_editor(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x14201C, 7);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x3A5C4E);

    // "Folha de papel"
    uint32_t px1 = ix + 14, py1 = iy + 8, pw = 22, ph = 30;
    fb_fill_rect(px1, py1, pw, ph, 0xE8F0E8);
    fb_fill_rect(px1, py1, pw, 3, 0xC8D8C8);
    // Linhas de texto simuladas
    fb_fill_rect(px1 + 3, py1 + 8,  pw - 8, 2, 0x8AA090);
    fb_fill_rect(px1 + 3, py1 + 13, pw - 8, 2, 0x8AA090);
    fb_fill_rect(px1 + 3, py1 + 18, pw - 12, 2, 0x8AA090);

    // "Caneta" na diagonal
    uint32_t qx = ix + ICON_W - 16, qy = iy + 10;
    fb_fill_rect(qx, qy, 4, 20, 0x50C090);
    fb_fill_circle(qx + 2, qy + 20, 3, 0x3FA070);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, tr(STR_ICON_EDITOR),
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_desktop_icons(void) {
    draw_icon_terminal(ICON_TERM_X, ICON_TERM_Y);
    draw_icon_about(ICON_ABOUT_X, ICON_ABOUT_Y);
    draw_icon_settings(ICON_CONF_X, ICON_CONF_Y);
    draw_icon_editor(ICON_EDIT_X, ICON_EDIT_Y);
}