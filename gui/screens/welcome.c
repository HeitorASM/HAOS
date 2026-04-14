// gui/screens/welcome.c — tela de boas-vindas
#include "welcome.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"

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

    uint32_t pw = 420, ph = 310;
    uint32_t px = (sw - pw) / 2, py = (sh - ph) / 2;

    fb_draw_shadow(px + 8, py + 8, pw, ph);
    fb_draw_rounded_rect(px, py, pw, ph, 0x0D1525, 10);
    fb_fill_rect(px + 2, py + 2, pw - 4, 1, 0x2244AA);
    fb_fill_gradient_v(px + 1, py + 1, pw - 2, 60, 0x0D2040, 0x0A1428);

    draw_logo_mini(sw/2 - 60, py + 12);

    fb_fill_gradient_h(px + 16, py + 62, pw - 32, 1, 0x0A1428, COLOR_ACCENT);
    fb_fill_gradient_h(px + 16, py + 63, pw - 32, 1, COLOR_ACCENT, 0x0A1428);

    fb_draw_string_centered(px, py + 74, pw, 22,
                             "Bem-vindo ao HAOS", COLOR_TEXT_LIGHT, 0, true);
    fb_draw_string_centered(px, py + 100, pw, 16,
                             "Home-built Operating System v1.1",
                             COLOR_TEXT_GRAY, 0, true);
    fb_draw_string_centered(px, py + 120, pw, 14,
                             "x86-64  |  PS/2 Keyboard + Mouse  |  VESA 32bpp",
                             COLOR_TEXT_DIM, 0, true);

    fb_fill_rect(px + 40, py + 145, pw - 80, 1, 0x1A2840);

    fb_draw_string_centered(px, py + 158, pw, 14,
                             "Double-buffered GUI  |  Janelas arrastáveis",
                             0x3A6A99, 0, true);
    fb_draw_string_centered(px, py + 174, pw, 14,
                             "Terminal integrado  |  Cursor do mouse",
                             0x3A6A99, 0, true);

    // Botão "Entrar"
    uint32_t btnw = 160, btnh = 38;
    uint32_t btnx = (sw - btnw) / 2, btny = py + ph - 66;
    fb_draw_rounded_rect(btnx, btny, btnw, btnh, COLOR_ACCENT_DARK, 8);
    fb_fill_gradient_v(btnx + 2, btny + 2, btnw - 4, btnh - 4,
                       0x3A7ACC, 0x1A4A90);
    fb_fill_rect(btnx + 4, btny + 2, btnw - 8, 1, 0x6AACFF);
    fb_draw_string_centered(btnx, btny, btnw, btnh,
                             "Entrar  [ENTER]", COLOR_TEXT_LIGHT, 0, true);

    fb_draw_string_centered(0, sh - 28, sw, 18,
                             "Pressione ENTER para continuar (ou aguarde 5s)",
                             COLOR_TEXT_DIM, 0, true);

    fb_flip();
}