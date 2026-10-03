#include "contextmenu.h"
#include "taskbar.h"
#include "../core/theme.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"

#define CM_W       200
#define CM_PAD     4     // moldura interna em volta dos itens
#define CM_ITEM_H  30

static bool    s_open = false;
static int32_t s_x = 0, s_y = 0;

// Mesmos textos do menu Iniciar / ícones do desktop (já traduzidos).
static const StrID s_labels[CONTEXTMENU_ITEM_COUNT] = {
    STR_TERMINAL, STR_ICON_EDITOR, STR_ICON_FILES, STR_SETTINGS, STR_ABOUT
};

static int32_t menu_height(void) {
    return CM_PAD * 2 + CONTEXTMENU_ITEM_COUNT * CM_ITEM_H;
}

void contextmenu_open(int32_t x, int32_t y) {
    int32_t sw     = (int32_t)fb_width();
    int32_t usable = (int32_t)fb_height() - (int32_t)TASKBAR_H; // área acima da taskbar
    int32_t h      = menu_height();

    if (x + CM_W > sw)     x = sw - CM_W;
    if (y + h    > usable) y = usable - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    s_x = x;
    s_y = y;
    s_open = true;
}

void contextmenu_close(void) { s_open = false; }
bool contextmenu_is_open(void) { return s_open; }

bool contextmenu_contains(int32_t mx, int32_t my) {
    return s_open &&
           mx >= s_x && mx < s_x + CM_W &&
           my >= s_y && my < s_y + menu_height();
}

int contextmenu_hit_test(int32_t mx, int32_t my) {
    if (!contextmenu_contains(mx, my)) return -1;
    if (mx < s_x + CM_PAD || mx >= s_x + CM_W - CM_PAD) return -1;

    int32_t rel = my - s_y - CM_PAD;
    if (rel < 0) return -1;
    int idx = (int)(rel / CM_ITEM_H);
    return idx < CONTEXTMENU_ITEM_COUNT ? idx : -1;
}

void draw_contextmenu(int32_t mouse_x, int32_t mouse_y) {
    if (!s_open) return;

    const Theme* t = theme_current();
    uint32_t x = (uint32_t)s_x, y = (uint32_t)s_y;
    uint32_t w = CM_W, h = (uint32_t)menu_height();

    if (t->window_shadow) fb_draw_shadow(x + 5, y + 5, w, h);

    // Moldura + miolo, como em draw_start_menu.
    fb_draw_rounded_rect(x, y, w, h, t->startmenu_border, 6);
    fb_fill_rect(x + 1, y + 1, w - 2, h - 2, t->startmenu_bg_bottom);

    int hover = contextmenu_hit_test(mouse_x, mouse_y);
    for (int i = 0; i < CONTEXTMENU_ITEM_COUNT; i++) {
        uint32_t iy = y + CM_PAD + (uint32_t)(i * CM_ITEM_H);
        uint32_t bg = (i == hover) ? t->accent_dark : t->startmenu_item_bg;

        fb_fill_rect(x + CM_PAD, iy, w - 2 * CM_PAD, CM_ITEM_H - 2, bg);
        fb_draw_string(x + CM_PAD + 8,
                       iy + (uint32_t)((CM_ITEM_H - 2 - (int)FONT_H) / 2),
                       tr(s_labels[i]), t->startmenu_text, 0, true);
    }
}
