#include "haos/uitheme.h"
#include "haos/gfx.h"

static uint32_t g_last_gen;

static void defaults(ui_theme_t* t) {              // tema escuro, caso a syscall falhe
    static const uint32_t d[UI_THEME_COUNT] = {
        1, 0x0F1220, 0x1B2538, 0x34415C, 0xE6EAF2, 0xB4BCCB, 0x7C869B, 0x4C9AFF, 0x2A6FD1,
        0x24324A, 0x2E3F5E, 0x1C2840, 0xE6EAF2, 0x3B4B69, 0x4C9AFF,
        0x0B0F1A, 0xE6EAF2, 0x34415C, 0x4C9AFF, 0xE6EAF2,
        0x0B0F1A, 0x34415C, 0x4C9AFF,
        0x151C2C, 0x1B2538, 0x2A6FD1, 0xE6EAF2,
        1, 4, 4, 8, 12, 28
    };
    for (int i = 0; i < UI_THEME_COUNT; i++) t->c[i] = d[i];
}

int ui_theme_load(ui_theme_t* t) {
    if (!t) return -1;
    defaults(t);
    int n = gfx_theme_get(t->c, UI_THEME_COUNT);
    if (n < 0) return -1;
    g_last_gen = t->c[UI_THEME_GENERATION];
    return 0;
}

int ui_theme_changed(void) {
    uint32_t gen = 0;
    if (gfx_theme_get(&gen, 1) < 0) return 0;
    return gen != g_last_gen;
}
