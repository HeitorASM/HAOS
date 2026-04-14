#include "bootscreen.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include <stdint.h>

#define LOGO_SCALE  6   /* 8px × 6 = 48px tall glyphs */
#define LOGO_STR    "HAOS"

static void delay(volatile uint32_t n) {
    while (n--) __asm__ volatile("nop");
}

static void draw_progress(int pct, uint32_t w, uint32_t h) {
    int bar_w  = (int)w * 6 / 10;
    int bar_h  = 6;
    int bar_x  = ((int)w - bar_w) / 2;
    int bar_y  = (int)h - 80;

    /* Background track */
    fb_fill_rect(bar_x, bar_y, bar_w, bar_h, COL_DGRAY);
    /* Progress fill */
    int fill = bar_w * pct / 100;
    fb_fill_rect(bar_x, bar_y, fill, bar_h, COL_ACCENT);
    /* Border */
    fb_draw_rect(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, COL_GRAY);
}

void boot_screen_run(void) {
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();

    fb_clear(COL_BLACK);

    /* Draw "HAOS" logo centered */
    int lw = font_str_width(LOGO_STR, LOGO_SCALE);
    int lx = ((int)w - lw) / 2;
    int ly = (int)h / 2 - (FONT_H * LOGO_SCALE) / 2 - 40;
    font_puts_scaled(lx, ly, LOGO_STR, COL_ACCENT, COL_BLACK, LOGO_SCALE);

    /* Subtitle */
    const char* sub = "Starting...";
    int sw = font_str_width(sub, 1);
    font_puts((int)w/2 - sw/2, ly + FONT_H * LOGO_SCALE + 24,
              sub, COL_GRAY, COL_BLACK);

    /* Animated progress bar */
    for (int pct = 0; pct <= 100; pct++) {
        draw_progress(pct, w, h);
        delay(300000UL);
    }
}
