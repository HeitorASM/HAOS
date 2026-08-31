// ============================================================
//  wm.cpp — Window Manager do HAOS
//
//  Migra a lógica de window.c (hit-testing de titlebar/botões,
//  drag, foco, z-order) para trabalhar com a classe Window nova,
//  despachando WidgetEvent para o Container/Widget Core em vez de
//  chamar callbacks fixos (on_click/on_drag/on_mouse_up).
// ============================================================

#include "wm.h"
#include "../kernel/memory.h"
#include "../drivers/fb.h"

namespace {

Window* s_windows[MAX_WINDOWS] = { nullptr };
int     s_focused_idx = -1;

int find_free_slot() {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (!s_windows[i]) return i;
    return -1;
}

int index_of(Window* win) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_windows[i] == win) return i;
    return -1;
}

// ---- Hit-tests (mesma geometria de window.c) ----

bool in_titlebar(Window* win, int32_t mx, int32_t my) {
    return mx >= win->bounds.x + (int32_t)Window::BORDER &&
           mx <  win->bounds.x + (int32_t)win->bounds.w - (int32_t)Window::BORDER &&
           my >= win->bounds.y + (int32_t)Window::BORDER &&
           my <  win->bounds.y + (int32_t)Window::BORDER + (int32_t)Window::TITLE_BAR_H;
}

bool in_close_btn(Window* win, int32_t mx, int32_t my) {
    int32_t bx = win->bounds.x + (int32_t)win->bounds.w - (int32_t)Window::BORDER
               - (int32_t)Window::BTN_SIZE - (int32_t)Window::BTN_GAP;
    int32_t by = win->bounds.y + (int32_t)Window::BORDER
               + ((int32_t)Window::TITLE_BAR_H - (int32_t)Window::BTN_SIZE) / 2;
    return mx >= bx && mx < bx + (int32_t)Window::BTN_SIZE &&
           my >= by && my < by + (int32_t)Window::BTN_SIZE;
}

bool in_min_btn(Window* win, int32_t mx, int32_t my) {
    int32_t bx = win->bounds.x + (int32_t)win->bounds.w - (int32_t)Window::BORDER
               - ((int32_t)Window::BTN_SIZE + (int32_t)Window::BTN_GAP) * 3
               + (int32_t)Window::BTN_GAP;
    int32_t by = win->bounds.y + (int32_t)Window::BORDER
               + ((int32_t)Window::TITLE_BAR_H - (int32_t)Window::BTN_SIZE) / 2;
    return mx >= bx && mx < bx + (int32_t)Window::BTN_SIZE &&
           my >= by && my < by + (int32_t)Window::BTN_SIZE;
}

bool in_content(Window* win, int32_t mx, int32_t my) {
    Rect c = win->content_area_absolute();
    return c.contains(mx, my);
}

bool in_window(Window* win, int32_t mx, int32_t my) {
    return win->bounds.contains(mx, my);
}

// Rastreia, por janela, se o botão foi pressionado dentro da área
// de conteúdo — necessário para saber se um MouseMove subsequente
// deve virar MouseDrag (equivalente ao on_drag antigo, usado p.ex.
// para seleção de texto) e se soltar o botão deve gerar MouseUp
// para os widgets internos.
bool s_content_mouse_down[MAX_WINDOWS] = { false };

} // namespace anônimo

void wm_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        s_windows[i] = nullptr;
        s_content_mouse_down[i] = false;
    }
    s_focused_idx = -1;
}

Window* wm_create(WinType type, int32_t x, int32_t y,
                  uint32_t w, uint32_t h, const char* title) {
    int slot = find_free_slot();
    if (slot < 0) return nullptr;

    Window* win = new Window(x, y, w, h, title, type);
    s_windows[slot] = win;
    wm_focus(win);
    return win;
}

bool wm_register(Window* win) {
    if (!win) return false;
    int slot = find_free_slot();
    if (slot < 0) return false;

    s_windows[slot] = win;
    wm_focus(win);
    return true;
}

void wm_close(Window* win) {
    if (!win) return;
    int idx = index_of(win);
    if (idx < 0) return;

    win->active = false;
    if (s_focused_idx == idx) s_focused_idx = -1;

    delete win; // destrutor de Container já limpa os widgets filhos
    s_windows[idx] = nullptr;
    s_content_mouse_down[idx] = false;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i]) { wm_focus(s_windows[i]); break; }
    }
}

void wm_focus(Window* win) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i]) s_windows[i]->focused = false;
    }
    if (win) {
        win->focused  = true;
        s_focused_idx = index_of(win);
    } else {
        s_focused_idx = -1;
    }
}

Window* wm_get_focused(void) {
    if (s_focused_idx < 0) return nullptr;
    Window* w = s_windows[s_focused_idx];
    return (w && w->active) ? w : nullptr;
}

int wm_active_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_windows[i] && s_windows[i]->active) n++;
    return n;
}

// ---- Mouse events ----

bool wm_mouse_down(int32_t mx, int32_t my) {
    for (int i = 0; i < MAX_WINDOWS; i++) s_content_mouse_down[i] = false;

    // Testa a janela focada primeiro (ela está desenhada por cima,
    // então deve interceptar cliques antes de qualquer outra).
    if (s_focused_idx >= 0 && s_windows[s_focused_idx] && s_windows[s_focused_idx]->active) {
        Window* win = s_windows[s_focused_idx];
        if (in_window(win, mx, my)) {
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_min_btn(win, mx, my))   { win->minimized = true; return true; }
            if (in_titlebar(win, mx, my)) {
                win->dragging = true;
                win->drag_ox  = mx - win->bounds.x;
                win->drag_oy  = my - win->bounds.y;
                return true;
            }
            if (!win->minimized && in_content(win, mx, my)) {
                s_content_mouse_down[s_focused_idx] = true;
                WidgetEvent ev{EventType::MouseDown, mx, my, 0};
                // Container::on_event espera coordenadas relativas
                // ao PAI da janela (0,0 = canto da tela); a própria
                // janela soma bounds.x/y internamente ao despachar
                // para os filhos, então passamos as coordenadas
                // absolutas de tela diretamente.
                win->on_event(ev);
            }
            return true;
        }
    }

    // Outras janelas (de trás para frente = topo do z-order primeiro
    // entre as não focadas — preserva o comportamento original).
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* win = s_windows[i];
        if (!win || !win->active || win->focused) continue;
        if (in_window(win, mx, my)) {
            wm_focus(win);
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_min_btn(win, mx, my))   { win->minimized = true; return true; }
            if (in_titlebar(win, mx, my)) {
                win->dragging = true;
                win->drag_ox  = mx - win->bounds.x;
                win->drag_oy  = my - win->bounds.y;
            }
            return true;
        }
    }
    return false;
}

void wm_mouse_move(int32_t mx, int32_t my) {
    Window* win = wm_get_focused();
    if (!win) return;

    if (win->dragging) {
        win->bounds.x = mx - win->drag_ox;
        win->bounds.y = my - win->drag_oy;
        if (win->bounds.x < 0) win->bounds.x = 0;
        if (win->bounds.y < 0) win->bounds.y = 0;
        int32_t sw = (int32_t)fb_width(),  sh = (int32_t)fb_height();
        if (win->bounds.x + (int32_t)win->bounds.w > sw)
            win->bounds.x = sw - (int32_t)win->bounds.w;
        if (win->bounds.y + (int32_t)win->bounds.h > sh)
            win->bounds.y = sh - (int32_t)win->bounds.h;
        return;
    }

    if (s_focused_idx >= 0 && s_content_mouse_down[s_focused_idx]) {
        WidgetEvent ev{EventType::MouseDrag, mx, my, 0};
        win->on_event(ev);
    } else {
        WidgetEvent ev{EventType::MouseMove, mx, my, 0};
        win->on_event(ev);
    }
}

void wm_mouse_up(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window* win = s_windows[i];
        if (win && s_content_mouse_down[i]) {
            WidgetEvent ev{EventType::MouseUp, 0, 0, 0};
            win->on_event(ev);
        }
        if (win) win->dragging = false;
        s_content_mouse_down[i] = false;
    }
}

// ---- Desenho ----

void wm_draw_window(Window* win) {
    if (!win) return;
    win->draw(0, 0); // Window desenha em coordenadas absolutas (ox=oy=0)
}

void wm_draw_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (s_windows[i] && s_windows[i]->active && !s_windows[i]->focused)
            wm_draw_window(s_windows[i]);
    }
    if (s_focused_idx >= 0 && s_windows[s_focused_idx] && s_windows[s_focused_idx]->active) {
        wm_draw_window(s_windows[s_focused_idx]);
    }
}

void wm_dispatch_key(uint8_t c) {
    Window* w = wm_get_focused();
    if (!w) return;
    WidgetEvent ev{EventType::KeyDown, 0, 0, c};
    w->on_event(ev);
}
