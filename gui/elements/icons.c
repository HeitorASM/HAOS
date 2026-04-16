#include "icons.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"

void draw_icon_terminal(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x0D1925, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x2A4A6A);
    fb_fill_gradient_h(ix + 2, iy + 2, ICON_W - 4, 10, 0x1A3A60, 0x0D1925);

    fb_fill_circle(ix + 8,  iy + 7, 3, 0xE05555);
    fb_fill_circle(ix + 16, iy + 7, 3, 0xE0B840);
    fb_fill_circle(ix + 24, iy + 7, 3, 0x50B050);

    fb_fill_rect(ix + 2, iy + 13, ICON_W - 4, ICON_H - 15, COLOR_TERM_BG);
    fb_draw_string(ix + 5, iy + 16, ">", COLOR_TERM_PROMPT, 0, true);
    fb_draw_string(ix + 13, iy + 16, "_", COLOR_TERM_FG, 0, true);
    fb_draw_string(ix + 5, iy + 26, "$ haos", 0x446688, 0, true);
    fb_draw_string(ix + 5, iy + 36, "~$", 0x336655, 0, true);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Terminal",
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_icon_about(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x0E1830, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x2A4A7A);
    fb_fill_gradient_v(ix + 2, iy + 2, ICON_W - 4, ICON_H - 4, 0x0E1830, 0x0A1020);

    uint32_t cx = ix + ICON_W/2;
    fb_fill_circle(cx, iy + 14, 5, COLOR_ACCENT);
    fb_fill_circle(cx, iy + 14, 3, 0x0E1830);
    fb_fill_circle(cx, iy + 14, 2, COLOR_ACCENT);
    fb_fill_rect(cx - 3, iy + 22, 6, 16, COLOR_ACCENT);
    fb_fill_rect(cx - 5, iy + 35, 10, 4, COLOR_ACCENT);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Sobre",
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_icon_settings(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x111520, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x303848);

    uint32_t cx = ix + ICON_W/2, cy = iy + ICON_H/2;
    fb_fill_circle(cx, cy, 16, 0x3A5A7A);
    fb_fill_circle(cx, cy, 12, 0x111520);
    fb_fill_circle(cx, cy, 6, 0x3A5A7A);
    fb_fill_circle(cx, cy, 3, 0x111520);

    fb_fill_rect(cx - 2, iy + 4, 4, 6, 0x3A5A7A);
    fb_fill_rect(cx - 2, iy + ICON_H - 10, 4, 6, 0x3A5A7A);
    fb_fill_rect(ix + 4, cy - 2, 6, 4, 0x3A5A7A);
    fb_fill_rect(ix + ICON_W - 10, cy - 2, 6, 4, 0x3A5A7A);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Config.",
                             COLOR_TEXT_LIGHT, 0, true);
}

void draw_desktop_icons(void) {
    draw_icon_terminal(ICON_TERM_X, ICON_TERM_Y);
    draw_icon_about(ICON_ABOUT_X, ICON_ABOUT_Y);
    draw_icon_settings(ICON_CONF_X, ICON_CONF_Y);
}