// gui/apps/config.c — Janela de configurações do HAOS (wallpaper)
// Usa a API Window original do projeto (wm_create + draw_content + on_key).
// Interação por mouse é feita via wm_mouse_down (já existente no WM).

#include "config.h"
#include "../wallpaper.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../kernel/types.h"

#define CFG_W   400
#define CFG_H   300

#define CFG_BG      0x0F1220
#define CFG_ACCENT  0x5AA0FF
#define CFG_TEXT    0xDDEEFF
#define CFG_DIM     0x7799BB
#define CFG_SEL     0x1A3A6A
#define CFG_BORDER  0x2A4A8A

static Window* cfg_win = NULL;

// ── Posições da lista (relativas ao conteúdo da janela) ───────────────────
#define LIST_X   16
#define LIST_W   (CFG_W - 32)
#define LIST_Y   40      // relativo ao y do conteúdo (após titlebar)
#define ITEM_H   26

static inline int mode_section_y(void) {
    return LIST_Y + (wallpaper_count() + 1) * ITEM_H + 14;
}
#define MODE_W  108
#define MODE_H  26

// ── Desenha o conteúdo da janela ──────────────────────────────────────────
static void config_draw_content(Window* w) {
    // Coordenadas absolutas do conteúdo (abaixo da titlebar)
    int cx = w->x + WIN_BORDER;
    int cy = w->y + TITLE_BAR_H;
    int cw = (int)w->w - WIN_BORDER * 2;

    // Fundo
    fb_fill_rect((uint32_t)cx, (uint32_t)cy,
                 (uint32_t)cw, (uint32_t)(w->h - TITLE_BAR_H), CFG_BG);

    // Cabeçalho
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + 12),
                   "Papel de parede:", CFG_ACCENT, 0, true);

    int count = wallpaper_count();
    int cur   = wallpaper_get();

    // Item "Padrão"
    {
        int iy = cy + LIST_Y;
        bool sel = (cur == -1);
        fb_fill_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_SEL : CFG_BG);
        if (sel)
            fb_draw_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                         (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2), CFG_ACCENT, 1);
        fb_draw_string((uint32_t)(cx + LIST_X + 8), (uint32_t)(iy + 6),
                       "Padrao (gradiente)", sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    for (int i = 0; i < count; i++) {
        int iy = cy + LIST_Y + (i + 1) * ITEM_H;
        bool sel = (cur == i);
        fb_fill_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                     (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2),
                     sel ? CFG_SEL : CFG_BG);
        if (sel)
            fb_draw_rect((uint32_t)(cx + LIST_X), (uint32_t)iy,
                         (uint32_t)LIST_W, (uint32_t)(ITEM_H - 2), CFG_ACCENT, 1);
        fb_draw_string((uint32_t)(cx + LIST_X + 8), (uint32_t)(iy + 6),
                       wallpaper_name(i), sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    // Separador
    int sy = cy + mode_section_y() - 6;
    fb_fill_rect((uint32_t)(cx + 8), (uint32_t)sy, (uint32_t)(cw - 16), 1, CFG_BORDER);

    // Modos
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + mode_section_y()),
                   "Modo:", CFG_DIM, 0, true);

    static const char* mnames[] = { "Preencher", "Centralizar", "Lado a lado" };
    int cur_mode = (int)wallpaper_get_mode();
    for (int m = 0; m < 3; m++) {
        int bx = cx + LIST_X + m * (MODE_W + 8);
        int by = cy + mode_section_y() + 18;
        bool sel = (cur_mode == m);
        fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)MODE_W, (uint32_t)MODE_H,
                     sel ? CFG_SEL : CFG_BG);
        fb_draw_rect((uint32_t)bx, (uint32_t)by, (uint32_t)MODE_W, (uint32_t)MODE_H,
                     sel ? CFG_ACCENT : CFG_BORDER, 1);
        uint32_t tw = fb_text_width(mnames[m]);
        fb_draw_string((uint32_t)(bx + (MODE_W - (int32_t)tw) / 2), (uint32_t)(by + 6),
                       mnames[m], sel ? CFG_ACCENT : CFG_TEXT, 0, true);
    }

    // Rodapé
    fb_draw_string((uint32_t)(cx + LIST_X), (uint32_t)(cy + CFG_H - TITLE_BAR_H - 20),
                   "j/k: navegar  1/2/3: modo  ESC: fechar",
                   CFG_DIM, 0, true);
}

// ── Teclado ──────────────────────────────────────────────────────────────
static void config_on_key(Window* w, uint8_t c) {
    (void)w;
    int count = wallpaper_count();
    int cur   = wallpaper_get();

    if (c == 27) {                               // ESC
        wm_close(cfg_win); cfg_win = NULL; return;
    }
    if ((c == 'k' || c == 'K') && cur > -1)     wallpaper_set(cur - 1);
    if ((c == 'j' || c == 'J') && cur < count-1) wallpaper_set(cur + 1);
    if (c == '1') wallpaper_set_mode(WALLPAPER_MODE_FILL);
    if (c == '2') wallpaper_set_mode(WALLPAPER_MODE_CENTER);
    if (c == '3') wallpaper_set_mode(WALLPAPER_MODE_TILE);
}

// ── Abre a janela ─────────────────────────────────────────────────────────
void open_config_window(void) {
    if (cfg_win && cfg_win->active) { wm_focus(cfg_win); return; }

    uint32_t sw = fb_width(), sh = fb_height();
    int wx = (int)(sw / 2) - CFG_W / 2;
    int wy = (int)(sh / 2) - CFG_H / 2;

    cfg_win = wm_create(WIN_TYPE_DIALOG, wx, wy, CFG_W, CFG_H, "Configuracoes");
    if (!cfg_win) return;

    cfg_win->draw_content = config_draw_content;
    cfg_win->on_key       = config_on_key;

    wm_focus(cfg_win);
}