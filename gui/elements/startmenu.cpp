#include "startmenu.h"
#include "taskbar.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/lang.h"
#include "../../kernel/memory.h"

#define MENU_W        210
#define MENU_HEADER_H 44
#define MENU_ITEM_H   34
#define MENU_FOOTER_H 40  
#define MENU_ITEM_COUNT 7 

struct MenuItemDef { const char* prefix; int str_id; bool is_separator; };

static void build_items(const char** out_labels, char (*bufs)[40]) {
    kstrcpy(bufs[0], ""); kstrcat(bufs[0], tr(STR_TERMINAL));
    kstrcpy(bufs[1], ""); kstrcat(bufs[1], tr(STR_ABOUT));
    kstrcpy(bufs[2], ""); kstrcat(bufs[2], tr(STR_ICON_EDITOR));
    kstrcpy(bufs[3], ""); kstrcat(bufs[3], tr(STR_SETTINGS));
    bufs[4][0] = 0; // separador
    kstrcpy(bufs[5], ""); kstrcat(bufs[5], tr(STR_RESTART));
    kstrcpy(bufs[6], ""); kstrcat(bufs[6], tr(STR_SHUTDOWN));
    for (int i = 0; i < MENU_ITEM_COUNT; i++) out_labels[i] = bufs[i];
}

static uint32_t menu_content_height() {
    return MENU_HEADER_H + MENU_ITEM_COUNT * MENU_ITEM_H + MENU_FOOTER_H;
}

static uint32_t menu_origin_y() {
    uint32_t sh = fb_height();
    return sh - TASKBAR_H - menu_content_height();
}

void draw_start_menu(void) {
    uint32_t mw = MENU_W;
    uint32_t mh = menu_content_height(); // calculado, não mais hardcoded
    uint32_t mx = 4;
    uint32_t my = menu_origin_y();

    fb_draw_shadow(mx + 6, my + 6, mw, mh);
    fb_draw_rounded_rect(mx, my, mw, mh, 0x0C1220, 9);
    fb_fill_rect(mx + 1, my + 1, mw - 2, 1, 0x3A5AA0);

    fb_fill_gradient_v(mx + 1, my + 1, mw - 2, MENU_HEADER_H, 0x0F2648, 0x0A1220);
    fb_fill_rect(mx + 1, my + MENU_HEADER_H, mw - 2, 1, 0x223A66);
    fb_draw_string_centered(mx, my + 1, mw, MENU_HEADER_H,
                             tr(STR_STARTMENU_HEADER), COLOR_TEXT_LIGHT, 0, true);

    char bufs[MENU_ITEM_COUNT][40];
    const char* items[MENU_ITEM_COUNT];
    build_items(items, bufs);

    int iy = (int)my + (int)MENU_HEADER_H + 6;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        if (!items[i][0]) {
            fb_fill_rect(mx + 12, (uint32_t)(iy + MENU_ITEM_H/2), mw - 24, 1, 0x223A66);
        } else {
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, MENU_ITEM_H - 2, 0x0E1830);
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, 1, 0x1E3560);
            fb_draw_string(mx + 10, (uint32_t)(iy + (int)(MENU_ITEM_H - FONT_H)/2),
                           items[i], COLOR_TEXT_LIGHT, 0, true);
        }
        iy += (int)MENU_ITEM_H;
    }

    fb_draw_string_centered(mx, (uint32_t)(my + mh - MENU_FOOTER_H + 4), mw, 18,
                             tr(STR_MENU_CLOSE_HINT),
                             COLOR_TEXT_DIM, 0, true);
}

int start_menu_hit_test(int32_t mx_click, int32_t my_click) {
    uint32_t mw = MENU_W;
    uint32_t mx = 4;
    uint32_t my = menu_origin_y();

    if (mx_click < (int32_t)mx || mx_click >= (int32_t)(mx + mw)) return -1;

    int iy = (int)my + (int)MENU_HEADER_H + 6;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        // Separador (índice 4) não é clicável — só ocupa espaço.
        bool is_separator = (i == 4);
        if (!is_separator &&
            my_click >= iy && my_click < iy + (int)MENU_ITEM_H - 2) {
            return i;
        }
        iy += (int)MENU_ITEM_H;
    }
    return -1;
}
