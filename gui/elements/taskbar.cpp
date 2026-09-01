#include "taskbar.h"
#include "../wm.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../drivers/rtc.h"
#include "../../kernel/lang.h"
#include "../../kernel/memory.h"

// Recorta uma string para caber em max_chars, adicionando "..." se
// precisar truncar — evita que títulos longos estourem o item da
// taskbar (que tem largura fixa TASKBAR_ITEM_W).
static void truncate_title(const char* src, char* out, int max_chars) {
    int len = (int)kstrlen(src);
    if (len <= max_chars) {
        kstrcpy(out, src);
        return;
    }
    int keep = max_chars - 3;
    if (keep < 1) keep = 1;
    for (int i = 0; i < keep; i++) out[i] = src[i];
    out[keep] = 0;
    kstrcat(out, "...");
}

// Calcula o x onde o item de índice `slot_visible_index` começa —
// usado tanto por draw_taskbar quanto por taskbar_hit_test, para
// garantir que a área clicável bate exatamente com o que foi
// desenhado (mesmo cálculo, uma fonte de verdade).
static int32_t item_x_at(int visible_index) {
    return (int32_t)(START_BTN_W + 12 + visible_index * (TASKBAR_ITEM_W + TASKBAR_ITEM_GAP));
}

void draw_taskbar(uint64_t ticks, bool start_menu_open) {
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

    // ---- Um item por JANELA ATIVA (substitui o bool único
    //      "terminal_active" da versão anterior). Percorre todos
    //      os slots do WM; janelas minimizadas continuam aparecendo
    //      (com um estilo visual diferente), exatamente como
    //      Windows/AROS fazem — minimizar não fecha, só esconde. ----
    int visible_index = 0;
    for (int i = 0; i < wm_window_slot_count(); i++) {
        Window* win = wm_get_window_at(i);
        if (!win || !win->active) continue;

        int32_t ax = item_x_at(visible_index);
        // Evita desenhar itens fora da tela se houver muitas janelas
        // (a taskbar simplesmente para de desenhar mais itens — uma
        // versão futura poderia rolar ou agrupar, mas isso já evita
        // sobrepor o relógio).
        if ((uint32_t)ax + TASKBAR_ITEM_W > sw - CLOCK_W - 16) break;

        bool is_focused = win->focused && !win->minimized;

        uint32_t bg = is_focused ? 0x1E3E78
                     : win->minimized ? 0x0A0E18
                                      : 0x0E1626;
        uint32_t border = is_focused ? COLOR_ACCENT : 0x2A4A7A;
        uint32_t text_col = win->minimized ? COLOR_TEXT_DIM : COLOR_TEXT_LIGHT;

        fb_draw_rounded_rect((uint32_t)ax, ty + 5, TASKBAR_ITEM_W, TASKBAR_H - 10, bg, 5);
        if (is_focused)
            fb_fill_rect((uint32_t)ax, ty + 5, TASKBAR_ITEM_W, 1, 0x5A96E0);
        else
            fb_fill_rect((uint32_t)ax, ty + 5, TASKBAR_ITEM_W, 1, border);

        char short_title[24];
        truncate_title(win->title, short_title, 18);
        fb_draw_string_centered((uint32_t)ax, ty + 5, TASKBAR_ITEM_W, TASKBAR_H - 10,
                                 short_title, text_col, 0, true);

        // Pequeno indicador (barrinha à esquerda) para janela focada
        // — reforça visualmente qual delas está em primeiro plano,
        // já que várias podem estar com o mesmo fundo "não minimizada".
        if (is_focused) {
            fb_fill_rect((uint32_t)ax + 2, ty + 8, 3, TASKBAR_H - 16, COLOR_ACCENT);
        }

        visible_index++;
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

Window* taskbar_hit_test(int32_t mx, int32_t my) {
    uint32_t sh = fb_height();
    uint32_t ty = sh - TASKBAR_H;

    if (my < (int32_t)(ty + 5) || my >= (int32_t)(ty + TASKBAR_H - 5)) return nullptr;

    int visible_index = 0;
    for (int i = 0; i < wm_window_slot_count(); i++) {
        Window* win = wm_get_window_at(i);
        if (!win || !win->active) continue;

        int32_t ax = item_x_at(visible_index);
        if (mx >= ax && mx < ax + (int32_t)TASKBAR_ITEM_W) {
            return win;
        }
        visible_index++;
    }
    return nullptr;
}
