#include "wm.h"
#include "../../kernel/memory.h"
#include "../../drivers/fb.h"
#include "../elements/taskbar.h"
#include "theme.h"

// Contador de ticks do PIT (100 Hz) — definido em kernel/idt_asm.asm.
// Usado para detectar duplo-clique na barra de título.
extern "C" volatile uint64_t timer_ticks;

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

// ---- Hit-tests de chrome ----

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

bool in_max_btn(Window* win, int32_t mx, int32_t my) {
    int32_t bx = win->bounds.x + (int32_t)win->bounds.w - (int32_t)Window::BORDER
               - ((int32_t)Window::BTN_SIZE + (int32_t)Window::BTN_GAP) * 2;
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

// Detecta em qual(is) borda(s) o cursor está, dentro da faixa
// RESIZE_GRIP ao redor do retângulo da janela. Retorna 0 se o
// cursor não está em nenhuma borda (ex.: está no meio do conteúdo).
// Cantos retornam a combinação de duas bordas (ex.: canto inferior
// direito = RESIZE_RIGHT | RESIZE_BOTTOM), permitindo redimensionar
// as duas dimensões ao mesmo tempo arrastando o canto.
uint8_t hit_test_resize_edge(Window* win, int32_t mx, int32_t my) {
    if (win->maximized) return 0; // não faz sentido redimensionar maximizada

    int32_t x = win->bounds.x, y = win->bounds.y;
    int32_t w = (int32_t)win->bounds.w, h = (int32_t)win->bounds.h;
    int32_t g = (int32_t)Window::RESIZE_GRIP;

    // Fora da faixa de borda (nem perto do retângulo) -> sem resize
    bool near_left   = mx >= x - g && mx < x + g;
    bool near_right  = mx >= x + w - g && mx < x + w + g;
    bool near_top    = my >= y - g && my < y + g;
    bool near_bottom = my >= y + h - g && my < y + h + g;

    bool within_x = mx >= x - g && mx < x + w + g;
    bool within_y = my >= y - g && my < y + h + g;

    uint8_t edge = 0;
    if (near_left   && within_y) edge |= Window::RESIZE_LEFT;
    if (near_right  && within_y) edge |= Window::RESIZE_RIGHT;
    if (near_top    && within_x) edge |= Window::RESIZE_TOP;
    if (near_bottom && within_x) edge |= Window::RESIZE_BOTTOM;
    return edge;
}

// Rastreia, por janela, se o botão foi pressionado dentro da área
// de conteúdo — necessário para saber se um MouseMove subsequente
// deve virar MouseDrag (equivalente ao on_drag antigo, usado p.ex.
// para seleção de texto) e se soltar o botão deve gerar MouseUp
// para os widgets internos.
// ---- Duplo-clique na barra de título / snap nas bordas ----
//
// Só pode haver UM arrasto de janela por vez (um único ponteiro), então
// este estado é global ao WM em vez de ficar dentro de cada Window.

int32_t s_press_x = 0, s_press_y = 0;   // onde o botão foi pressionado na titlebar
Rect    s_drag_origin = {0, 0, 0, 0};   // geometria "normal" da janela ao começar o arrasto

Window*  s_click_win  = nullptr;        // última janela clicada na titlebar
uint64_t s_click_tick = 0;
int32_t  s_click_x = 0, s_click_y = 0;

const uint64_t DOUBLE_CLICK_TICKS = 40; // 400 ms (PIT a 100 Hz)
const int32_t  DOUBLE_CLICK_SLOP  = 5;  // tolerância de movimento entre os cliques, em px
const int32_t  DRAG_START_DIST    = 5;  // movimento mínimo para "soltar" uma janela maximizada/encaixada
const int32_t  SNAP_EDGE          = 2;  // o cursor a <= 2 px da borda da tela ativa o snap

enum SnapZone { SNAP_ZONE_NONE = 0, SNAP_ZONE_LEFT, SNAP_ZONE_RIGHT, SNAP_ZONE_TOP };
SnapZone s_snap_zone = SNAP_ZONE_NONE;  // zona sob o cursor durante um arrasto

// Registra um clique na barra de título de `win` e diz se ele completa
// um duplo-clique (mesma janela, dentro de 400 ms e de 5 px do clique
// anterior). Um duplo-clique "consome" o par: o terceiro clique começa
// uma nova contagem em vez de virar outro duplo.
bool title_double_click(Window* win, int32_t mx, int32_t my) {
    uint64_t now = timer_ticks;
    int32_t dx = mx - s_click_x; if (dx < 0) dx = -dx;
    int32_t dy = my - s_click_y; if (dy < 0) dy = -dy;

    bool dbl = (s_click_win == win) &&
               (now - s_click_tick <= DOUBLE_CLICK_TICKS) &&
               dx <= DOUBLE_CLICK_SLOP && dy <= DOUBLE_CLICK_SLOP;
    if (dbl) {
        s_click_win = nullptr;
    } else {
        s_click_win  = win;
        s_click_tick = now;
        s_click_x    = mx;
        s_click_y    = my;
    }
    return dbl;
}

// Começa a arrastar `win` pela barra de título.
void begin_titlebar_drag(Window* win, int32_t mx, int32_t my) {
    s_press_x = mx;
    s_press_y = my;
    // Se a janela já está maximizada/encaixada, a geometria "normal" é a
    // salva; senão, é a atual. É para onde ela volta ao ser restaurada
    // depois de um snap — SEM isso, saved_bounds guardaria a posição
    // onde a janela estava no instante de soltar (já arrastada até a
    // borda), e restaurar a colocaria num lugar estranho.
    s_drag_origin = (win->maximized || win->snapped) ? win->saved_bounds : win->bounds;
    win->dragging = true;
    win->drag_ox  = mx - win->bounds.x;
    win->drag_oy  = my - win->bounds.y;
}

// Retângulo que a janela ocuparia se encaixada em `zone` (a área útil
// termina em cima da taskbar).
Rect snap_rect(SnapZone zone) {
    int32_t sw = (int32_t)fb_width();
    int32_t sh = (int32_t)fb_height() - (int32_t)TASKBAR_H;
    switch (zone) {
        case SNAP_ZONE_LEFT:  return Rect{0,      0, (uint32_t)(sw / 2),       (uint32_t)sh};
        case SNAP_ZONE_RIGHT: return Rect{sw / 2, 0, (uint32_t)(sw - sw / 2),  (uint32_t)sh};
        default:              return Rect{0,      0, (uint32_t)sw,             (uint32_t)sh};
    }
}

SnapZone snap_zone_at(int32_t mx, int32_t my) {
    int32_t sw = (int32_t)fb_width();
    if (my <= SNAP_EDGE)          return SNAP_ZONE_TOP;    // topo tem prioridade sobre os cantos
    if (mx <= SNAP_EDGE)          return SNAP_ZONE_LEFT;
    if (mx >= sw - 1 - SNAP_EDGE) return SNAP_ZONE_RIGHT;
    return SNAP_ZONE_NONE;
}

// Encaixa `win` em `zone` (chamado ao SOLTAR uma janela arrastada).
void apply_snap(Window* win, SnapZone zone) {
    if (zone == SNAP_ZONE_TOP) {
        if (!win->maximized) wm_maximize_toggle(win);
        win->saved_bounds = s_drag_origin;   // restaurar volta para onde estava ANTES do arrasto
        return;
    }
    win->saved_bounds = s_drag_origin;
    win->bounds  = snap_rect(zone);
    win->snapped = (zone == SNAP_ZONE_LEFT) ? Window::SNAP_LEFT : Window::SNAP_RIGHT;
    win->on_resized();
}

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
    if (!win) return nullptr; // heap esgotada — ver comentário em
                                // terminal_create (com -fno-exceptions,
                                // `new` devolve nullptr em vez de lançar)
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

// ---- Fila de ações diferidas (ver comentário extenso em wm.h) ----
#define WM_MAX_DEFERRED 8

namespace {
struct DeferredAction {
    wm_deferred_fn fn;
    void*          arg;
};
DeferredAction s_deferred[WM_MAX_DEFERRED];
int            s_deferred_count = 0;
}

bool wm_defer(wm_deferred_fn fn, void* arg) {
    if (!fn) return false;
    if (s_deferred_count >= WM_MAX_DEFERRED) return false;
    s_deferred[s_deferred_count].fn  = fn;
    s_deferred[s_deferred_count].arg = arg;
    s_deferred_count++;
    return true;
}

void wm_run_deferred_actions(void) {
    // Copia e zera a fila ANTES de executar qualquer ação: se uma
    // ação registrar outra ação diferida (perfeitamente legítimo),
    // a nova entra na fila já vazia e roda só na PRÓXIMA passagem —
    // nunca dentro desta. Sem essa cópia, uma ação que se
    // re-registrasse a si mesma criaria um laço infinito aqui
    // dentro, travando o sistema.
    DeferredAction pending[WM_MAX_DEFERRED];
    int count = s_deferred_count;
    for (int i = 0; i < count; i++) pending[i] = s_deferred[i];
    s_deferred_count = 0;

    for (int i = 0; i < count; i++) {
        if (pending[i].fn) pending[i].fn(pending[i].arg);
    }
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

Window* wm_get_window_at(int index) {
    if (index < 0 || index >= MAX_WINDOWS) return nullptr;
    return s_windows[index];
}

int wm_window_slot_count(void) {
    return MAX_WINDOWS;
}

// ---- Minimizar / Maximizar / Restaurar ----

void wm_minimize(Window* win) {
    if (!win) return;
    win->minimized = true;
    // Ao minimizar a janela focada, tira o foco dela — nenhuma
    // janela minimizada deveria continuar recebendo teclado. A
    // taskbar (ou outra janela clicada) é quem vai focar de novo.
    if (win->focused) wm_focus(nullptr);
}

void wm_restore(Window* win) {
    if (!win) return;
    win->minimized = false;
    if (win->maximized) {
        win->maximized = false;
        win->bounds = win->saved_bounds;
        win->on_resized();
    }
    wm_focus(win);
}

void wm_unminimize(Window* win) {
    if (!win) return;
    win->minimized = false;
    wm_focus(win);
}

void wm_focus_next(void) {
    // Próximo slot ocupado depois do focado (com volta ao início). Se
    // nada está focado (s_focused_idx == -1), começa do slot 0.
    for (int step = 1; step <= MAX_WINDOWS; step++) {
        int i = (s_focused_idx + step + MAX_WINDOWS) % MAX_WINDOWS;
        Window* w = s_windows[i];
        if (!w || !w->active || i == s_focused_idx) continue;
        w->minimized = false;   // Alt+Tab também traz de volta uma minimizada
        wm_focus(w);
        return;
    }
}

Window* wm_window_at(int32_t mx, int32_t my) {
    if (s_focused_idx >= 0) {
        Window* f = s_windows[s_focused_idx];
        if (f && f->active && !f->minimized && in_window(f, mx, my)) return f;
    }
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* w = s_windows[i];
        if (!w || !w->active || w->focused || w->minimized) continue;
        if (in_window(w, mx, my)) return w;
    }
    return nullptr;
}

void wm_maximize_toggle(Window* win) {
    if (!win) return;

    if (win->maximized) {
        win->maximized = false;
        win->bounds = win->saved_bounds;
    } else {
        // Janela encaixada já tem em saved_bounds o tamanho "normal";
        // não sobrescrever com a metade da tela.
        if (!win->snapped) win->saved_bounds = win->bounds;
        win->snapped   = Window::SNAP_NONE;
        win->maximized = true;
        uint32_t sw = fb_width(), sh = fb_height();
        // Reserva espaço para a taskbar na parte inferior.
        win->bounds = Rect{0, 0, sw, sh - TASKBAR_H};
    }
    win->minimized = false;
    win->on_resized();
}

// ---- Mouse events ----

bool wm_mouse_down(int32_t mx, int32_t my) {
    for (int i = 0; i < MAX_WINDOWS; i++) s_content_mouse_down[i] = false;
    s_snap_zone = SNAP_ZONE_NONE;

    // Testa a janela focada primeiro (ela está desenhada por cima,
    // então deve interceptar cliques antes de qualquer outra).
    if (s_focused_idx >= 0 && s_windows[s_focused_idx] && s_windows[s_focused_idx]->active
        && !s_windows[s_focused_idx]->minimized) {
        Window* win = s_windows[s_focused_idx];

        // Redimensionar tem prioridade sobre o resto do hit-test da
        // janela: a faixa de RESIZE_GRIP fica levemente por fora/
        // sobre a borda, então precisa ser checada antes de decidir
        // se o clique "está na janela" no sentido tradicional.
        uint8_t edge = hit_test_resize_edge(win, mx, my);
        if (edge != 0) {
            win->resizing    = true;
            win->resize_edge = edge;
            return true;
        }

        if (in_window(win, mx, my)) {
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_max_btn(win, mx, my))   { wm_maximize_toggle(win); return true; }
            if (in_min_btn(win, mx, my))   { wm_minimize(win); return true; }
            if (in_titlebar(win, mx, my)) {
                // Duplo-clique na barra de título alterna maximizar/
                // restaurar. Tem que ser decidido ANTES de soltar uma
                // janela maximizada para arrastar (o que agora só
                // acontece quando o mouse realmente se move — ver
                // wm_mouse_move), senão o primeiro clique já a
                // desmaximizaria e o segundo a maximizaria de novo.
                if (title_double_click(win, mx, my)) {
                    wm_maximize_toggle(win);
                    return true;
                }
                begin_titlebar_drag(win, mx, my);
                return true;
            }
            if (!win->minimized && in_content(win, mx, my)) {
                s_content_mouse_down[s_focused_idx] = true;
                WidgetEvent ev{EventType::MouseDown, mx, my, 0, 0};
                // Window::on_event() converte estas coordenadas
                // absolutas de tela para relativas à própria janela
                // antes de despachar à árvore de widgets (ver
                // window.cpp) — o resto da cadeia (Panel, VStack,
                // Button...) trabalha de forma consistente em
                // coordenadas relativas ao pai imediato.
                win->on_event(ev);
            }
            return true;
        }
    }

    // Outras janelas (de trás para frente = topo do z-order primeiro
    // entre as não focadas — preserva o comportamento original).
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        Window* win = s_windows[i];
        if (!win || !win->active || win->focused || win->minimized) continue;
        if (in_window(win, mx, my)) {
            wm_focus(win);
            if (in_close_btn(win, mx, my)) { wm_close(win); return true; }
            if (in_max_btn(win, mx, my))   { wm_maximize_toggle(win); return true; }
            if (in_min_btn(win, mx, my))   { wm_minimize(win); return true; }
            if (in_titlebar(win, mx, my)) {
                if (title_double_click(win, mx, my)) wm_maximize_toggle(win);
                else begin_titlebar_drag(win, mx, my);
            }
            return true;
        }
    }
    return false;
}

bool wm_mouse_right_down(int32_t mx, int32_t my) {
    Window* win = wm_window_at(mx, my);
    if (!win) return false;
    if (!win->focused) wm_focus(win);
    if (!win->minimized && in_content(win, mx, my)) {
        WidgetEvent ev{EventType::MouseRightDown, mx, my, 0, 0};
        win->on_event(ev);
    }
    return true;
}

void wm_mouse_move(int32_t mx, int32_t my) {
    Window* win = wm_get_focused();
    if (!win) return;

    if (win->resizing) {
        Rect r = win->bounds;
        uint8_t edge = win->resize_edge;

        if (edge & Window::RESIZE_RIGHT) {
            int32_t new_w = mx - r.x;
            r.w = new_w < (int32_t)win->min_width
                ? win->min_width : (uint32_t)new_w;
        }
        if (edge & Window::RESIZE_BOTTOM) {
            int32_t new_h = my - r.y;
            r.h = new_h < (int32_t)win->min_height
                ? win->min_height : (uint32_t)new_h;
        }
        if (edge & Window::RESIZE_LEFT) {
            int32_t new_w = (r.x + (int32_t)r.w) - mx;
            if (new_w < (int32_t)win->min_width) {
                r.x = r.x + (int32_t)r.w - (int32_t)win->min_width;
                r.w = win->min_width;
            } else {
                r.x = mx;
                r.w = (uint32_t)new_w;
            }
        }
        if (edge & Window::RESIZE_TOP) {
            int32_t new_h = (r.y + (int32_t)r.h) - my;
            if (new_h < (int32_t)win->min_height) {
                r.y = r.y + (int32_t)r.h - (int32_t)win->min_height;
                r.h = win->min_height;
            } else {
                r.y = my;
                r.h = (uint32_t)new_h;
            }
        }

        win->snapped = Window::SNAP_NONE; // redimensionar à mão desfaz o encaixe
        win->bounds = r;
        win->on_resized();
        return;
    }

    if (win->dragging) {
        // Janela maximizada ou encaixada: só "solta" do estado depois
        // que o mouse realmente se moveu (>= 5 px desde o clique).
        // wm_mouse_move roda a cada frame com o botão pressionado, mesmo
        // sem movimento — soltar já no primeiro frame desmaximizaria a
        // janela num simples clique e quebraria o duplo-clique.
        if (win->maximized || win->snapped) {
            int32_t dx = mx - s_press_x;
            int32_t dy = my - s_press_y;
            if (dx * dx + dy * dy < DRAG_START_DIST * DRAG_START_DIST) return;

            int32_t old_w = (int32_t)win->bounds.w;
            int32_t rel_x = win->drag_ox;   // onde, na barra, o usuário agarrou (definido no clique)
            win->maximized = false;
            win->snapped   = Window::SNAP_NONE;
            win->bounds.w  = win->saved_bounds.w;
            win->bounds.h  = win->saved_bounds.h;
            // Mantém a MESMA proporção horizontal do cursor sobre a barra
            // de título (agarrou no meio -> continua no meio).
            win->bounds.x  = old_w > 0
                ? mx - (int32_t)((int64_t)rel_x * (int64_t)win->bounds.w / old_w)
                : mx;
            win->drag_ox   = mx - win->bounds.x;
            win->on_resized();
        }

        win->bounds.x = mx - win->drag_ox;
        win->bounds.y = my - win->drag_oy;
        if (win->bounds.x < 0) win->bounds.x = 0;
        if (win->bounds.y < 0) win->bounds.y = 0;
        int32_t sw = (int32_t)fb_width(),  sh = (int32_t)fb_height();
        if (win->bounds.x + (int32_t)win->bounds.w > sw)
            win->bounds.x = sw - (int32_t)win->bounds.w;
        if (win->bounds.y + (int32_t)win->bounds.h > sh)
            win->bounds.y = sh - (int32_t)win->bounds.h;

        // Cursor encostado numa borda da tela: ao soltar, a janela
        // encaixa ali (wm_mouse_up). wm_draw_all mostra o contorno.
        // Só conta depois de um arrasto de verdade (>= 5 px desde o
        // clique): uma janela rente à borda recebe wm_mouse_move todo
        // frame com o botão apertado, e um simples clique na titlebar
        // (cursor a <= 2 px da borda) não pode virar um snap.
        int32_t mdx = mx - s_press_x;
        int32_t mdy = my - s_press_y;
        bool dragged_far = mdx * mdx + mdy * mdy >= DRAG_START_DIST * DRAG_START_DIST;
        s_snap_zone = dragged_far ? snap_zone_at(mx, my) : SNAP_ZONE_NONE;
        return;
    }

    if (s_focused_idx >= 0 && s_content_mouse_down[s_focused_idx]) {
        WidgetEvent ev{EventType::MouseDrag, mx, my, 0, 0};
        win->on_event(ev);
    } else {
        WidgetEvent ev{EventType::MouseMove, mx, my, 0, 0};
        win->on_event(ev);
    }
}

void wm_mouse_scroll(int32_t mx, int32_t my, int32_t amount) {
    Window* win = wm_get_focused();
    if (!win || amount == 0 || !in_content(win, mx, my)) return;
    WidgetEvent ev{EventType::MouseScroll, mx, my, 0, amount};
    win->on_event(ev);
}

void wm_mouse_up(int32_t mx, int32_t my) {
    // Soltou uma janela arrastada com o cursor numa borda: encaixa.
    if (s_snap_zone != SNAP_ZONE_NONE) {
        Window* dragged = wm_get_focused();
        if (dragged && dragged->dragging) apply_snap(dragged, s_snap_zone);
        s_snap_zone = SNAP_ZONE_NONE;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        Window* win = s_windows[i];
        if (win && s_content_mouse_down[i]) {
            // FIX: antes usava ev.x=0, ev.y=0 (posição zerada), o que
            // fazia bounds.contains() falhar em qualquer widget real
            // dentro da árvore — MouseUp nunca "acertava" o botão, e
            // por isso o callback de clique (que dispara no MouseUp,
            // não no MouseDown) nunca era chamado. O "efeito visual"
            // de pressionar funcionava (MouseDown tem posição real),
            // mas a ação nunca disparava. Agora usa a posição atual
            // real do cursor, igual MouseDown/MouseMove/MouseDrag já
            // faziam.
            WidgetEvent ev{EventType::MouseUp, mx, my, 0, 0};
            win->on_event(ev);
        }
        if (win) {
            win->dragging     = false;
            win->resizing     = false;
            win->resize_edge  = 0;
        }
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

    // Contorno da área onde a janela arrastada vai encaixar ao soltar.
    if (s_snap_zone != SNAP_ZONE_NONE) {
        Window* dragged = wm_get_focused();
        if (dragged && dragged->dragging) {
            Rect r = snap_rect(s_snap_zone);
            fb_draw_rect((uint32_t)r.x, (uint32_t)r.y, r.w, r.h,
                         theme_current()->accent, 3);
        }
    }
}

// ---- Cursor hint (hover sobre borda redimensionável) ----

CursorType wm_get_cursor_hint(int32_t mx, int32_t my) {
    Window* win = wm_get_focused();
    if (!win || win->minimized) return CURSOR_NORMAL;

    // Já arrastando uma borda: mantém o cursor de resize consistente
    // com a borda que está sendo arrastada, mesmo que o mouse se
    // afaste um pouco da faixa de grip durante o movimento rápido.
    uint8_t edge = win->resizing ? win->resize_edge
                                 : hit_test_resize_edge(win, mx, my);
    if (edge == 0) return CURSOR_NORMAL;

    bool horiz = edge & (Window::RESIZE_LEFT | Window::RESIZE_RIGHT);
    bool vert  = edge & (Window::RESIZE_TOP  | Window::RESIZE_BOTTOM);

    if (horiz && vert) {
        // Canto: escolhe a diagonal certa.
        //   topo-esquerdo / baixo-direito      -> "\" (DIAG1)
        //   topo-direito  / baixo-esquerdo     -> "/" (DIAG2)
        bool top_left     = edge & Window::RESIZE_LEFT  && edge & Window::RESIZE_TOP;
        bool bottom_right = edge & Window::RESIZE_RIGHT && edge & Window::RESIZE_BOTTOM;
        return (top_left || bottom_right) ? CURSOR_RESIZE_DIAG1 : CURSOR_RESIZE_DIAG2;
    }
    if (horiz) return CURSOR_RESIZE_H;
    if (vert)  return CURSOR_RESIZE_V;
    return CURSOR_NORMAL;
}

void wm_dispatch_key(uint8_t c) {
    Window* w = wm_get_focused();
    if (!w) return;
    WidgetEvent ev{EventType::KeyDown, 0, 0, c, 0};
    w->on_event(ev);
}
