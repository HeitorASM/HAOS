#include "welcome.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"

static void draw_logo_mini(uint32_t x, uint32_t y) {
    // Desenha as letras HAOS em miniatura
    fb_fill_rect(x,      y, 5, 28, COLOR_ACCENT);
    fb_fill_rect(x,      y+11, 14, 6, COLOR_ACCENT);
    fb_fill_rect(x+9,    y, 5, 28, COLOR_ACCENT);
    x += 20;
    fb_fill_rect(x+5,    y, 5, 28, COLOR_ACCENT);
    fb_fill_rect(x,      y+11, 15, 6, COLOR_ACCENT);
    fb_fill_rect(x,      y, 6, 17, COLOR_ACCENT);
    fb_fill_rect(x+9,    y, 6, 17, COLOR_ACCENT);
    x += 20;
    fb_draw_rounded_rect(x, y, 16, 28, COLOR_ACCENT, 5);
    fb_draw_rounded_rect(x+3, y+3, 10, 22, 0x0A1428, 4);
    x += 22;
    fb_fill_rect(x,      y,    15, 5, COLOR_ACCENT);
    fb_fill_rect(x,      y+5,  6, 6, COLOR_ACCENT);
    fb_fill_rect(x,      y+11, 15, 6, COLOR_ACCENT);
    fb_fill_rect(x+9,    y+17, 6, 6, COLOR_ACCENT);
    fb_fill_rect(x,      y+22, 15, 6, COLOR_ACCENT);
}

void draw_welcome_screen(void) {
    uint32_t sw = fb_width(), sh = fb_height();

    fb_fill_gradient_v(0, 0, sw, sh, 0x060810, 0x0E1428);
    fb_fill_gradient_v(0, 0, sw, 80, 0x0A1830, 0x060810);

    uint32_t pw = 440, ph = 330;
    uint32_t px = (sw - pw) / 2, py = (sh - ph) / 2;

    // Glow suave por trás do cartão (dá profundidade sem pesar)
    fb_draw_shadow(px + 10, py + 10, pw, ph);
    fb_draw_rounded_rect(px - 2, py - 2, pw + 4, ph + 4, 0x14264A, 12);
    fb_draw_rounded_rect(px, py, pw, ph, 0x0D1525, 10);
    fb_fill_rect(px + 2, py + 2, pw - 4, 1, 0x2A50A0);
    fb_fill_gradient_v(px + 1, py + 1, pw - 2, 64, 0x0D2040, 0x0A1428);

    draw_logo_mini(sw/2 - 60, py + 16);

    fb_fill_gradient_h(px + 16, py + 68, pw - 32, 1, 0x0A1428, COLOR_ACCENT);
    fb_fill_gradient_h(px + 16, py + 69, pw - 32, 1, COLOR_ACCENT, 0x0A1428);

    fb_draw_string_centered(px, py + 82, pw, 22,
                             tr(STR_WELCOME_TITLE), COLOR_TEXT_LIGHT, 0, true);
    fb_draw_string_centered(px, py + 108, pw, 16,
                             tr(STR_WELCOME_SUBTITLE),
                             COLOR_TEXT_GRAY, 0, true);
    fb_draw_string_centered(px, py + 130, pw, 14,
                             tr(STR_WELCOME_SPECS),
                             COLOR_TEXT_DIM, 0, true);

    fb_fill_rect(px + 40, py + 158, pw - 80, 1, 0x1A2840);

    fb_draw_string_centered(px, py + 172, pw, 14,
                             tr(STR_WELCOME_FEATURE_1),
                             0x4A7AB0, 0, true);
    fb_draw_string_centered(px, py + 190, pw, 14,
                             tr(STR_WELCOME_FEATURE_2),
                             0x4A7AB0, 0, true);

    // Botão "Entrar"
    uint32_t btnw = 170, btnh = 40;
    uint32_t btnx = (sw - btnw) / 2, btny = py + ph - 68;
    fb_draw_shadow(btnx + 3, btny + 4, btnw, btnh);
    fb_draw_rounded_rect(btnx, btny, btnw, btnh, COLOR_ACCENT_DARK, 9);
    fb_fill_gradient_v(btnx + 2, btny + 2, btnw - 4, btnh - 4,
                       0x4A88DD, 0x1A4A90);
    fb_fill_rect(btnx + 4, btny + 2, btnw - 8, 1, 0x7AC0FF);
    fb_draw_string_centered(btnx, btny, btnw, btnh,
                             tr(STR_WELCOME_ENTER_BTN), COLOR_TEXT_LIGHT, 0, true);

    fb_draw_string_centered(0, sh - 30, sw, 18,
                             tr(STR_WELCOME_HINT),
                             COLOR_TEXT_DIM, 0, true);

    fb_flip();
}