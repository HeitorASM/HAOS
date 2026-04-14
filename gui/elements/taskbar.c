// gui/elements/taskbar.c — barra de tarefas
#include "taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"

void draw_taskbar(uint64_t ticks, bool start_menu_open, bool terminal_active) {
    uint32_t sw = fb_width(), sh = fb_height();
    uint32_t ty = sh - TASKBAR_H;

    fb_fill_gradient_v(0, ty, sw, TASKBAR_H, 0x0A0E1A, 0x060810);
    fb_fill_rect(0, ty, sw, 1, COLOR_TASKBAR_LINE);
    fb_fill_gradient_h(0, ty + 1, sw, 1, 0x1A2840, 0x0A0E1A);

    // Botão Iniciar
    uint32_t sb_col = start_menu_open ? COLOR_ACCENT_DARK : 0x0E1A30;
    fb_draw_rounded_rect(4, ty + 5, START_BTN_W, TASKBAR_H - 10, sb_col, 5);
    if (start_menu_open)
        fb_fill_rect(4, ty + 5, START_BTN_W, 1, 0x4A80CC);
    fb_draw_string_centered(4, ty + 5, START_BTN_W, TASKBAR_H - 10,
                             "# Iniciar", COLOR_TEXT_LIGHT, 0, true);

    // Botão do terminal
    int ax = START_BTN_W + 12;
    if (terminal_active) {
        // Obtém foco via wm_get_focused (mas aqui usamos apenas uma cor estática)
        uint32_t tbtn = 0x0C1220; // simplificado
        fb_draw_rounded_rect((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10, tbtn, 4);
        fb_draw_string_centered((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10,
                                 "Terminal", COLOR_TEXT_LIGHT, 0, true);
    }

    // Relógio digital
    char tbuf[12];
    uint64_t secs  = ticks / 100;
    uint64_t mins  = secs  / 60;
    uint64_t hours = mins  / 60;
    secs  %= 60; mins %= 60; hours %= 24;
    tbuf[0] = '0' + (char)(hours/10); tbuf[1] = '0' + (char)(hours%10);
    tbuf[2] = ':';
    tbuf[3] = '0' + (char)(mins/10);  tbuf[4] = '0' + (char)(mins%10);
    tbuf[5] = ':';
    tbuf[6] = '0' + (char)(secs/10);  tbuf[7] = '0' + (char)(secs%10);
    tbuf[8] = 0;

    uint32_t clock_x = sw - CLOCK_W - 8;
    fb_draw_rounded_rect(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                         0x0C1220, 4);
    fb_draw_string_centered(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                             tbuf, COLOR_ACCENT, 0, true);
}