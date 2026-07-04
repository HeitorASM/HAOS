#include "taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../drivers/rtc.h"
#include "../../kernel/lang.h"

void draw_taskbar(uint64_t ticks, bool start_menu_open, bool terminal_active) {
    (void)ticks;  // não usamos mais ticks para o relógio
    uint32_t sw = fb_width(), sh = fb_height();
    uint32_t ty = sh - TASKBAR_H;

    fb_fill_gradient_v(0, ty, sw, TASKBAR_H, 0x0C1020, 0x060810);
    fb_fill_rect(0, ty, sw, 1, COLOR_TASKBAR_LINE);
    fb_fill_gradient_h(0, ty + 1, sw, 1, 0x223A66, 0x0A0E1A);

    // Botão Iniciar
    uint32_t sb_col = start_menu_open ? COLOR_ACCENT_DARK : 0x101E38;
    fb_draw_rounded_rect(4, ty + 5, START_BTN_W, TASKBAR_H - 10, sb_col, 6);
    if (start_menu_open)
        fb_fill_rect(4, ty + 5, START_BTN_W, 1, 0x5A96E0);
    fb_draw_string_centered(4, ty + 5, START_BTN_W, TASKBAR_H - 10,
                             tr(STR_START_BUTTON), COLOR_TEXT_LIGHT, 0, true);

    // Botão do terminal
    int ax = START_BTN_W + 12;
    if (terminal_active) {
        uint32_t tbtn = 0x0E1626;
        fb_draw_rounded_rect((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10, tbtn, 5);
        fb_fill_rect((uint32_t)ax, ty + 5, 130, 1, 0x2A4A7A);
        fb_draw_string_centered((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10,
                                 tr(STR_TERMINAL), COLOR_TEXT_LIGHT, 0, true);
    }

    // Relógio digital (RTC real)
    rtc_time_t rt;
    rtc_read_time(&rt);
    char tbuf[9];
    rtc_format_time(tbuf, &rt);    // "HH:MM:SS"

    uint32_t clock_x = sw - CLOCK_W - 8;
    fb_draw_rounded_rect(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                         0x0E1626, 5);
    fb_fill_rect(clock_x - 4, ty + 5, CLOCK_W + 8, 1, 0x2A4A7A);
    fb_draw_string_centered(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                             tbuf, COLOR_ACCENT, 0, true);
}