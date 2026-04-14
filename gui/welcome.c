#include "welcome.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/keyboard.h"
#include <stdint.h>

static void delay(volatile uint32_t n) { while (n--) __asm__ volatile("nop"); }

void welcome_screen_run(void) {
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();

    /* Dark blue gradient (simulated with horizontal bands) */
    for (uint32_t y = 0; y < h; y++) {
        uint32_t col = fb_mix(0x00001133, 0x00003366, (int)(y * 255 / h));
        fb_draw_hline(0, (int)y, (int)w, col);
    }

    /* "Welcome to HAOS" - large */
    const char* title = "Welcome to HAOS";
    int tw = font_str_width(title, 3);
    int ty = (int)h / 2 - 60;
    font_puts_scaled((int)w/2 - tw/2, ty, title, COL_WHITE, 0x00000000, 3);

    /* Version line */
    const char* ver = "Version 0.1 - A custom OS";
    int vw = font_str_width(ver, 1);
    font_puts((int)w/2 - vw/2, ty + FONT_H*3 + 20, ver, COL_LGRAY, 0x00000000);

    /* Separator */
    fb_draw_hline((int)w/2 - 200, ty + FONT_H*3 + 36, 400, COL_ACCENT);

    /* Blink "Press any key..." */
    const char* prompt = "Press any key to continue...";
    int pw = font_str_width(prompt, 1);
    int py = ty + FONT_H*3 + 60;
    int visible = 1;
    uint32_t blink_cnt = 0;

    while (!keyboard_poll()) {
        blink_cnt++;
        if (blink_cnt % 500000 == 0) {
            visible ^= 1;
            uint32_t bg = fb_mix(0x00001133, 0x00003366, (int)((uint32_t)py * 255 / h));
            font_puts((int)w/2 - pw/2, py, prompt,
                      visible ? COL_LGRAY : bg, 0x00000000);
        }
        delay(1);
    }
    keyboard_getchar(); /* consume the keypress */
}
