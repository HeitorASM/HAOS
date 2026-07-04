// gui/apps/about.c — janela "Sobre o HAOS"
#include "about.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"

static void about_draw(Window* win) {
    int bx = win->x + WIN_BORDER + 16;
    int by = win->y + WIN_BORDER + TITLE_BAR_H + 16;

    fb_draw_string((uint32_t)bx, (uint32_t)by,
                   tr(STR_ABOUT_HEADER), COLOR_ACCENT, 0, true);
    by += 6;
    fb_fill_rect((uint32_t)bx, (uint32_t)by, 180, 1, 0x2A4A8A);
    by += 12;

    const char* lines[] = {
        tr(STR_ABOUT_VERSION),
        tr(STR_ABOUT_ARCH),
        tr(STR_ABOUT_BOOT),
        tr(STR_ABOUT_VIDEO),
        tr(STR_ABOUT_GUI),
        tr(STR_ABOUT_INPUT),
        tr(STR_ABOUT_KERNEL),
        "",
        tr(STR_ABOUT_FOOTER),
    };
    for (int i = 0; i < 9; i++) {
        uint32_t col = (lines[i][0] == 0) ? 0 : (i == 8 ? COLOR_TEXT_GRAY : COLOR_TEXT_LIGHT);
        if (lines[i][0])
            fb_draw_string((uint32_t)bx, (uint32_t)by, lines[i], col, 0, true);
        by += 20;
    }
}

void open_about_window(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    Window* w = wm_create(WIN_TYPE_ABOUT,
                          (int32_t)(sw/2 - 220), (int32_t)(sh/2 - 150),
                          440, 300, tr(STR_ABOUT_WINDOW_TITLE));
    if (w) {
        w->draw_content = about_draw;
        w->on_key = NULL;
    }
}