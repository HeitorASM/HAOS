// gui/apps/config.c — Janela de configurações do HAOS
// Exibe RAM total, RAM livre, CPU (via sysinfo), opções de
// papel de parede e seleção de idioma (PT/EN).

#include "config.h"
#include "../wallpaper.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../kernel/types.h"
#include "../../kernel/memory.h"
#include "../../kernel/sysinfo.h"
#include "../../kernel/lang.h"

#define CFG_W   440
#define CFG_H   430

#define CFG_BG      0x0E1220
#define CFG_ACCENT  0x5AA0FF
#define CFG_TEXT    0xE4F0FF
#define CFG_DIM     0x86A0C0
#define CFG_SEL     0x1E3E78
#define CFG_BORDER  0x33528E

static Window* cfg_win = NULL;

#define LIST_X   16
#define LIST_W   (CFG_W - 32)
#define LIST_Y   40
#define ITEM_H   26

static inline int mode_section_y(void) {
    return LIST_Y + (wallpaper_count() + 1) * ITEM_H + 14;
}
#define MODE_W  108
#define MODE_H  26

// ---- Secção de idioma ----
// Aparece abaixo dos modos de wallpaper
static inline int lang_section_y(void) {
    return mode_section_y() + MODE_H + 30;
}
#define LANG_W  108
#define LANG_H  26

// ---- Secção de informações de hardware ----
// Aparece abaixo do idioma
static inline int hw_section_y(void) {
    return lang_section_y() + LANG_H + 30;
}

// ---- Desenha uma linha "Chave: Valor" ----
static void draw_info_row(int cx, int cy, int y, const char* key, const char* val) {
    fb_draw_string((uint32_t)(cx + LIST_X),      (uint32_t)(cy + y), key, CFG_DIM,  0, true);
    fb_draw_string((uint32_t)(cx + LIST_X + 120), (uint32_t)(cy + y), val, CFG_TEXT, 0, true);
}

// ---- Conteúdo principal ----
static void config_draw_content(Window* w) {
    int cx = w->x + WIN_BORDER;
    int cy = w->y + TITLE_BAR_H;
    int cw = (int)w->w - WIN_BORDER * 2;

    // Fundo
    fb_fill_rect((uint32_t)cx, (uint32_t)cy,
                 (uint32_t)cw, (uint32_t)(w->h - TITLE_BAR_H), CFG_BG);

    // ---- Secção: Papel de parede ----
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + 12),
                   tr(STR_CONFIG_WALLPAPER), CFG_ACCENT, 0, true);

    int count = wallpaper_count();
    int cur   = wallpaper_get();

    // Item "Padrão"
    {
        int iy  = cy + LIST_Y;
        bool sel = (cur == -1);
        fb_fill_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_SEL : CFG_BG);
        fb_draw_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_ACCENT : 0x1C2740, 1);
        fb_draw_string((uint32_t)(cx + LIST_X + 8), (uint32_t)(iy + 6),
                       tr(STR_CONFIG_DEFAULT_WALLPAPER), sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    for (int i = 0; i < count; i++) {
        int iy  = cy + LIST_Y + (i + 1) * ITEM_H;
        bool sel = (cur == i);
        fb_fill_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_SEL : CFG_BG);
        fb_draw_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_ACCENT : 0x1C2740, 1);
        fb_draw_string((uint32_t)(cx + LIST_X + 8), (uint32_t)(iy + 6),
                       wallpaper_name(i), sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    // Separador 1
    int sy = cy + mode_section_y() - 6;
    fb_fill_rect((uint32_t)(cx + 8), (uint32_t)sy, (uint32_t)(cw - 16), 1, CFG_BORDER);

    // ---- Secção: Modos ----
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + mode_section_y()),
                   tr(STR_CONFIG_MODE), CFG_DIM, 0, true);

    const char* mnames[] = { tr(STR_CONFIG_MODE_FILL), tr(STR_CONFIG_MODE_CENTER), tr(STR_CONFIG_MODE_TILE) };
    int cur_mode = (int)wallpaper_get_mode();
    for (int m = 0; m < 3; m++) {
        int bx  = cx + LIST_X + m * (MODE_W + 8);
        int by  = cy + mode_section_y() + 18;
        bool sel = (cur_mode == m);
        fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)MODE_W, (uint32_t)MODE_H,
                     sel ? CFG_SEL : CFG_BG);
        fb_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)MODE_W, (uint32_t)MODE_H,
                     sel ? CFG_ACCENT : CFG_BORDER, 1);
        uint32_t tw = fb_text_width(mnames[m]);
        fb_draw_string((uint32_t)(bx + (MODE_W - (int32_t)tw) / 2), (uint32_t)(by + 6),
                       mnames[m], sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    // Separador 2
    int sy15 = cy + lang_section_y() - 8;
    fb_fill_rect((uint32_t)(cx + 8), (uint32_t)sy15, (uint32_t)(cw - 16), 1, CFG_BORDER);

    // ---- Secção: Idioma ----
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + lang_section_y()),
                   tr(STR_CONFIG_LANGUAGE), CFG_DIM, 0, true);

    const char* lnames[] = { tr(STR_CONFIG_LANG_PT), tr(STR_CONFIG_LANG_EN) };
    Lang cur_lang = lang_get();
    for (int l = 0; l < 2; l++) {
        int bx  = cx + LIST_X + l * (LANG_W + 8);
        int by  = cy + lang_section_y() + 18;
        bool sel = ((int)cur_lang == l);
        fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)LANG_W, (uint32_t)LANG_H,
                     sel ? CFG_SEL : CFG_BG);
        fb_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)LANG_W, (uint32_t)LANG_H,
                     sel ? CFG_ACCENT : CFG_BORDER, 1);
        uint32_t tw = fb_text_width(lnames[l]);
        fb_draw_string((uint32_t)(bx + (LANG_W - (int32_t)tw) / 2), (uint32_t)(by + 6),
                       lnames[l], sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    // Separador 3
    int sy2 = cy + hw_section_y() - 8;
    fb_fill_rect((uint32_t)(cx + 8), (uint32_t)sy2, (uint32_t)(cw - 16), 1, CFG_BORDER);

    // ---- Secção: Hardware ----
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + hw_section_y()),
                   tr(STR_CONFIG_DEVICE), CFG_ACCENT, 0, true);

    int hw_y = hw_section_y() + 18;

    // CPU Name
    draw_info_row(cx, cy, hw_y,      tr(STR_CONFIG_CPU), sysinfo_cpu_name());
    draw_info_row(cx, cy, hw_y + 18, tr(STR_CONFIG_CORES), "");

    // Número de núcleos (converte uint32 para string)
    char cores_buf[8];
    uint32_t cores = sysinfo_cpu_cores();
    kuitoa((uint64_t)cores, cores_buf);
    fb_draw_string((uint32_t)(cx + LIST_X + 120), (uint32_t)(cy + hw_y + 18),
                   cores_buf, CFG_TEXT, 0, true);

    // RAM Total
    char ram_buf[24];
    sysinfo_format_ram(sysinfo_total_ram(), ram_buf);
    draw_info_row(cx, cy, hw_y + 36, tr(STR_CONFIG_RAM_TOTAL), ram_buf);

    // RAM Livre
    sysinfo_format_ram(sysinfo_free_ram(), ram_buf);
    draw_info_row(cx, cy, hw_y + 54, tr(STR_CONFIG_RAM_FREE), ram_buf);

    // Heap usada pelo kernel
    sysinfo_format_ram(mem_get_heap_used(), ram_buf);
    draw_info_row(cx, cy, hw_y + 72, tr(STR_CONFIG_HEAP), ram_buf);

    // Rodapé
    fb_draw_string((uint32_t)(cx + LIST_X),
                   (uint32_t)(cy + CFG_H - TITLE_BAR_H - 20),
                   tr(STR_CONFIG_FOOTER_HINT),
                   CFG_DIM, 0, true);
}

// ---- Mouse ----
static void config_on_click(Window* w, int32_t cx, int32_t cy) {
    int wx = w->x + WIN_BORDER;
    int wy = w->y + TITLE_BAR_H;
    int lx = cx - wx, ly = cy - wy;   // coordenadas relativas ao conteúdo

    int count = wallpaper_count();

    // Item "Padrão"
    if (lx >= LIST_X && lx < LIST_X + LIST_W &&
        ly >= LIST_Y && ly < LIST_Y + (ITEM_H - 2)) {
        wallpaper_set(-1);
        goto retitle;
    }
    // Itens de wallpaper
    for (int i = 0; i < count; i++) {
        int iy = LIST_Y + (i + 1) * ITEM_H;
        if (lx >= LIST_X && lx < LIST_X + LIST_W &&
            ly >= iy && ly < iy + (ITEM_H - 2)) {
            wallpaper_set(i);
            goto retitle;
        }
    }
    // Modos (Preencher / Centralizar / Lado a lado)
    for (int m = 0; m < 3; m++) {
        int bx = LIST_X + m * (MODE_W + 8);
        int by = mode_section_y() + 18;
        if (lx >= bx && lx < bx + MODE_W && ly >= by && ly < by + MODE_H) {
            wallpaper_set_mode((WallpaperMode)m);
            goto retitle;
        }
    }
    // Idioma (Português / English)
    for (int l = 0; l < 2; l++) {
        int bx = LIST_X + l * (LANG_W + 8);
        int by = lang_section_y() + 18;
        if (lx >= bx && lx < bx + LANG_W && ly >= by && ly < by + LANG_H) {
            lang_set((Lang)l);
            goto retitle;
        }
    }
    return;

retitle:
    kstrcpy(w->title, tr(STR_CONFIG_WINDOW_TITLE));
}

// ---- Teclado ----
static void config_on_key(Window* w, uint8_t c) {
    (void)w;
    int count = wallpaper_count();
    int cur   = wallpaper_get();

    if (c == 27) {
        wm_close(cfg_win); cfg_win = NULL; return;
    }
    if ((c == 'k' || c == 'K') && cur > -1)      wallpaper_set(cur - 1);
    if ((c == 'j' || c == 'J') && cur < count-1)  wallpaper_set(cur + 1);
    if (c == '1') wallpaper_set_mode(WALLPAPER_MODE_FILL);
    if (c == '2') wallpaper_set_mode(WALLPAPER_MODE_CENTER);
    if (c == '3') wallpaper_set_mode(WALLPAPER_MODE_TILE);
    if (c == 'l' || c == 'L') lang_toggle();
    if (w && cfg_win) kstrcpy(cfg_win->title, tr(STR_CONFIG_WINDOW_TITLE));
}

// ---- Abre a janela ----
void open_config_window(void) {
    if (cfg_win && cfg_win->active) { cfg_win->minimized = false; wm_focus(cfg_win); return; }

    uint32_t sw = fb_width(), sh = fb_height();
    int wx = (int)(sw / 2) - CFG_W / 2;
    int wy = (int)(sh / 2) - CFG_H / 2;

    cfg_win = wm_create(WIN_TYPE_DIALOG, wx, wy, CFG_W, CFG_H, tr(STR_CONFIG_WINDOW_TITLE));
    if (!cfg_win) return;

    cfg_win->draw_content = config_draw_content;
    cfg_win->on_key       = config_on_key;
    cfg_win->on_click     = config_on_click;

    wm_focus(cfg_win);
}
