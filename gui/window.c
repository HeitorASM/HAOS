#include "window.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../kernel/memory.h"
#include "../kernel/types.h"

static Window windows[MAX_WINDOWS];
static int    focused_idx = -1;

void wm_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        windows[i].active = false;
    focused_idx = -1;
}

Window* wm_create(WinType type, int32_t x, int32_t y,
                  uint32_t w, uint32_t h, const char* title) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            Window* win    = &windows[i];
            win->active    = true;
            win->focused   = false;
            win->minimized = false;
            win->dragging  = false;
            win->type      = type;
            win->x = x; win->y = y;
            win->w = w; win->h = h;
            win->content      = NULL;
            win->draw_content = NULL;
            win->on_key       = NULL;
            kstrcpy(win->title, title);
            wm_focus(win);
            return win;
        }
    }
    return NULL;
}

void wm_close(Window* win) {
    if (!win) return;
    win->active  = false;
    win->focused = false;
    if (focused_idx >= 0 && &windows[focused_idx] == win)
        focused_idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active) { wm_focus(&windows[i]); break; }
}

void wm_focus(Window* win) {
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;
    if (win) {
        win->focused = true;
        focused_idx  = (int)(win - windows);
    } else {
        focused_idx = -1;
    }
}

Window* wm_get_focused(void) {
    if (focused_idx < 0) return NULL;
    return windows[focused_idx].active ? &windows[focused_idx] : NULL;
}

int wm_active_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) if (windows[i].active) n++;
    return n;
}

// ---- Hit tests -------------------------------------------------

// Retorna true se (mx,my) está dentro da área de título de win
static bool in_titlebar(Window* win, int32_t mx, int32_t my) {
    return mx >= win->x + WIN_BORDER &&
           mx <  win->x + (int32_t)win->w - WIN_BORDER &&
           my >= win->y + WIN_BORDER &&
           my <  win->y + WIN_BORDER + TITLE_BAR_H;
}

// Retorna true se (mx,my) está dentro do botão Fechar
static bool in_close_btn(Window* win, int32_t mx, int32_t my) {
    int32_t bx = win->x + (int32_t)win->w - WIN_BORDER - BTN_SIZE - BTN_GAP;
    int32_t by = win->y + WIN_BORDER + (TITLE_BAR_H - BTN_SIZE) / 2;
    return mx >= bx && mx < bx + BTN_SIZE && my >= by && my < by + BTN_SIZE;
}

// Retorna true se (mx,my) está dentro de qualquer área da janela
static bool in_window(Window* win, int32_t mx, int32_t my) {
    return mx >= win->x && mx < win->x + (int32_t)win->w &&
           my >= win->y && my < win->y + (int32_t)win->h;
}

// ---- Mouse events ----------------------------------------------

bool wm_mouse_down(int32_t mx, int32_t my) {
    // Verifica na ordem inversa (janela no topo primeiro)
    // Testa janela focada primeiro
    if (focused_idx >= 0 && windows[focused_idx].active) {
        Window* win = &windows[focused_idx];
        if (in_window(win, mx, my)) {
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_titlebar(win, mx, my)) {
                win->dragging = true;
                win->drag_ox  = mx - win->x;
                win->drag_oy  = my - win->y;
            }
            return true;
        }
    }
    // Outras janelas
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* win = &windows[i];
        if (!win->active || win->focused) continue;
        if (in_window(win, mx, my)) {
            wm_focus(win);
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_titlebar(win, mx, my)) {
                win->dragging = true;
                win->drag_ox  = mx - win->x;
                win->drag_oy  = my - win->y;
            }
            return true;
        }
    }
    return false;
}

void wm_mouse_move(int32_t mx, int32_t my) {
    Window* win = wm_get_focused();
    if (!win || !win->dragging) return;
    win->x = mx - win->drag_ox;
    win->y = my - win->drag_oy;
    // Clampa para não sair da tela
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
    int32_t sw = (int32_t)fb_width(),  sh = (int32_t)fb_height();
    if (win->x + (int32_t)win->w > sw) win->x = sw - (int32_t)win->w;
    if (win->y + (int32_t)win->h > sh) win->y = sh - (int32_t)win->h;
}

void wm_mouse_up(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        windows[i].dragging = false;
}

// ---- Desenho de uma janela -------------------------------------

void wm_draw_window(Window* win) {
    if (!win->active || win->minimized) return;

    uint32_t x = (uint32_t)win->x, y = (uint32_t)win->y;
    uint32_t w = win->w, h = win->h;

    // --- Sombra (5px offset, stipple)
    fb_draw_shadow(x + 6, y + 6, w, h);

    // --- Borda externa (arredondada, 1px)
    uint32_t border_col = win->focused ? COLOR_WIN_BORDER_A : COLOR_WIN_BORDER;
    fb_draw_rounded_rect(x, y, w, h, border_col, 6);

    // --- Barra de título: gradiente do azul escuro ao médio
    uint32_t tc1 = win->focused ? 0x1A3A70 : 0x252535;
    uint32_t tc2 = win->focused ? 0x0D1E40 : 0x141420;
    fb_fill_gradient_v(x + WIN_BORDER, y + WIN_BORDER,
                       w - WIN_BORDER*2, TITLE_BAR_H, tc1, tc2);

    // Linha brilhante no topo da titlebar
    uint32_t shine = win->focused ? 0x3A6ABB : 0x303048;
    fb_fill_rect(x + WIN_BORDER, y + WIN_BORDER,
                 w - WIN_BORDER*2, 1, shine);

    // --- Título
    uint32_t title_col = win->focused ? COLOR_TEXT_LIGHT : COLOR_TEXT_GRAY;
    uint32_t tx = x + WIN_BORDER + BTN_SIZE*3 + BTN_GAP*3 + 8;
    uint32_t ty = y + WIN_BORDER + (TITLE_BAR_H - FONT_H) / 2;
    fb_draw_string(tx, ty, win->title, title_col, 0, true);

    // --- Botões de controle (círculos)
    int32_t by2 = (int32_t)(y + WIN_BORDER) + (TITLE_BAR_H - BTN_SIZE) / 2;
    // Fechar (vermelho)
    int32_t bx2 = (int32_t)(x + w - WIN_BORDER - BTN_SIZE - BTN_GAP);
    fb_fill_circle((uint32_t)(bx2 + BTN_SIZE/2), (uint32_t)(by2 + BTN_SIZE/2),
                   BTN_SIZE/2, 0xE05555);
    fb_draw_string((uint32_t)(bx2 + 4), (uint32_t)(by2 + 3),
                   "x", 0xFFAAAA, 0, true);
    // Maximizar (verde)
    bx2 -= BTN_SIZE + BTN_GAP;
    fb_fill_circle((uint32_t)(bx2 + BTN_SIZE/2), (uint32_t)(by2 + BTN_SIZE/2),
                   BTN_SIZE/2, 0x50B050);
    // Minimizar (amarelo)
    bx2 -= BTN_SIZE + BTN_GAP;
    fb_fill_circle((uint32_t)(bx2 + BTN_SIZE/2), (uint32_t)(by2 + BTN_SIZE/2),
                   BTN_SIZE/2, 0xE0B840);

    // --- Separador abaixo da titlebar
    fb_fill_rect(x + WIN_BORDER,
                 y + WIN_BORDER + TITLE_BAR_H,
                 w - WIN_BORDER*2, 1, 0x0A1428);

    // --- Área de conteúdo
    uint32_t cy  = y + WIN_BORDER + TITLE_BAR_H + 1;
    uint32_t cw  = w - WIN_BORDER * 2;
    uint32_t ch  = h - WIN_BORDER - TITLE_BAR_H - 1;
    uint32_t cbg = (win->type == WIN_TYPE_TERMINAL) ? COLOR_TERM_BG : COLOR_WIN_BG;
    fb_fill_rect(x + WIN_BORDER, cy, cw, ch, cbg);

    if (win->draw_content) win->draw_content(win);
}

void wm_draw_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && !windows[i].focused)
            wm_draw_window(&windows[i]);
    if (focused_idx >= 0 && windows[focused_idx].active)
        wm_draw_window(&windows[focused_idx]);
}

void wm_dispatch_key(uint8_t c) {
    Window* w = wm_get_focused();
    if (w && w->on_key) w->on_key(w, c);
}
