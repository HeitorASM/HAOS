#include "wm.h"
#include <cstdio>

extern "C" {
volatile uint64_t timer_ticks = 0;
uint32_t fb_width(void)  { return 1024; }
uint32_t fb_height(void) { return 768; }
char* kstrncpy(char* d, const char* s, size_t n) {
    size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d;
}
}

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { g_n++; if (!(c)) { g_fail++; printf("  FALHOU linha %d: %s\n", __LINE__, #c); } } while (0)

static void slow_click(int x, int y);
static void click(int x, int y) { wm_mouse_down(x, y); wm_mouse_up(x, y); }
static void slow_click(int x, int y) { timer_ticks += 100; click(x, y); }
static bool same(const Rect& r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && (int)r.w == w && (int)r.h == h;
}
static void drag(int x0, int y0, int x1, int y1) {   // press, move em passos, release
    timer_ticks += 100;
    wm_mouse_down(x0, y0);
    int steps = 10;
    for (int i = 1; i <= steps; i++) wm_mouse_move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
    wm_mouse_up(x1, y1);
}

int main() {
    printf("== duplo-clique na barra de titulo ==\n");
    wm_init();
    Window* a = wm_create(WinType::Generic, 100, 100, 400, 300, "A");
    CHECK(a);
    timer_ticks = 1000; click(200, 110);
    timer_ticks = 1010; click(200, 110);
    CHECK(a->maximized);
    CHECK(same(a->bounds, 0, 0, 1024, 728));
    timer_ticks = 1100; click(500, 10);
    timer_ticks = 1110; click(500, 10);
    CHECK(!a->maximized);
    CHECK(same(a->bounds, 100, 100, 400, 300));          // volta ao tamanho original
    timer_ticks = 2000; click(200, 110);
    timer_ticks = 2050; click(200, 110);                  // 500 ms: lento demais
    CHECK(!a->maximized);
    timer_ticks = 3000; click(200, 110);
    timer_ticks = 3005; click(260, 110);                  // 60 px de distancia: nao e duplo
    CHECK(!a->maximized);
    timer_ticks = 4000; click(200, 110); timer_ticks = 4005; click(200, 110);
    CHECK(a->maximized);
    timer_ticks = 4010; click(500, 10);                   // 3o clique: nao vira outro duplo
    CHECK(a->maximized);

    printf("== janela maximizada: clique simples nao desmaximiza; arrasto sim ==\n");
    // a esta maximizada (0,0,1024,728), saved = 100,100,400,300
    timer_ticks += 100;
    wm_mouse_down(500, 10);
    for (int i = 0; i < 20; i++) wm_mouse_move(500, 10);   // frames sem movimento
    CHECK(a->maximized);
    wm_mouse_up(500, 10);
    CHECK(a->maximized);
    timer_ticks += 100;
    wm_mouse_down(500, 10);
    wm_mouse_move(502, 11);                                 // < 5 px
    CHECK(a->maximized);
    wm_mouse_move(520, 40);                                 // arrasto de verdade
    CHECK(!a->maximized);
    CHECK((int)a->bounds.w == 400 && (int)a->bounds.h == 300);
    CHECK(a->bounds.x == 520 - 500 * 400 / 1024);          // mesma proporcao sob o cursor
    CHECK(a->bounds.y == 30);
    wm_mouse_up(520, 40);
    CHECK(a->snapped == Window::SNAP_NONE);

    printf("== snap nas bordas ==\n");
    a->bounds = Rect{100, 100, 400, 300}; a->maximized = false; a->snapped = 0; a->saved_bounds = a->bounds;
    drag(200, 110, 0, 300);                                 // solta na borda esquerda
    CHECK(a->snapped == Window::SNAP_LEFT);
    CHECK(same(a->bounds, 0, 0, 512, 728));
    CHECK(same(a->saved_bounds, a->saved_bounds.x, a->saved_bounds.y, 400, 300));
    drag(100, 10, 400, 300);                               // arrasta de volta: desencaixa
    CHECK(a->snapped == Window::SNAP_NONE);
    CHECK((int)a->bounds.w == 400 && (int)a->bounds.h == 300);

    a->bounds = Rect{100, 100, 400, 300}; a->saved_bounds = a->bounds;
    drag(200, 110, 1023, 300);                              // borda direita
    CHECK(a->snapped == Window::SNAP_RIGHT);
    CHECK(same(a->bounds, 512, 0, 512, 728));

    // snap duplo: encaixada -> arrasta direto para a outra borda: saved_bounds continua o tamanho normal
    drag(600, 10, 0, 300);
    CHECK(a->snapped == Window::SNAP_LEFT);
    CHECK((int)a->saved_bounds.w == 400 && (int)a->saved_bounds.h == 300);

    a->bounds = Rect{100, 100, 400, 300}; a->snapped = 0; a->saved_bounds = a->bounds;
    drag(200, 110, 300, 0);                                 // topo = maximiza
    CHECK(a->maximized);
    wm_maximize_toggle(a);                                  // restaura
    CHECK(!a->maximized && same(a->bounds, 100, 100, 400, 300));   // posicao de ANTES do arrasto

    // clique simples (sem arrasto) nunca encaixa
    a->bounds = Rect{100, 100, 400, 300}; a->snapped = 0;
    slow_click(200, 110);
    CHECK(a->snapped == Window::SNAP_NONE && !a->maximized);

    // maximizar uma janela encaixada nao perde o tamanho normal
    a->bounds = Rect{100, 100, 400, 300}; a->saved_bounds = a->bounds;
    drag(200, 110, 0, 300);
    CHECK(a->snapped == Window::SNAP_LEFT);
    wm_maximize_toggle(a);
    CHECK(a->maximized && a->snapped == Window::SNAP_NONE);
    wm_maximize_toggle(a);
    CHECK(same(a->bounds, 100, 100, 400, 300));

    // redimensionar a mao desfaz o encaixe
    drag(200, 110, 0, 300);
    CHECK(a->snapped == Window::SNAP_LEFT);
    drag(512, 300, 600, 300);                               // borda direita da janela encaixada
    CHECK(a->snapped == Window::SNAP_NONE);

    printf("== Alt+Tab / wm_window_at / unminimize ==\n");
    wm_init();
    Window* A = wm_create(WinType::Generic,  50,  50, 300, 200, "A");
    Window* B = wm_create(WinType::Generic, 200, 100, 300, 200, "B");
    Window* C = wm_create(WinType::Generic, 150,  80, 300, 200, "C");
    CHECK(wm_get_focused() == C);
    wm_focus_next(); CHECK(wm_get_focused() == A);
    wm_focus_next(); CHECK(wm_get_focused() == B);
    wm_focus_next(); CHECK(wm_get_focused() == C);
    wm_minimize(A);
    wm_focus_next(); CHECK(wm_get_focused() == A && !A->minimized);   // traz a minimizada de volta

    wm_focus(C);
    CHECK(wm_window_at(250, 150) == C);                    // C, B e A se sobrepoem: focada vence
    CHECK(wm_window_at(480, 250) == B);                    // so B
    CHECK(wm_window_at(900, 600) == nullptr);              // desktop livre
    wm_minimize(B);
    CHECK(wm_window_at(480, 250) == nullptr);              // minimizada nao conta

    Window* S = wm_create(WinType::Generic, 0, 0, 200, 150, "S");
    wm_close(S);
    wm_init();
    Window* only = wm_create(WinType::Generic, 10, 10, 200, 150, "only");
    wm_focus_next(); CHECK(wm_get_focused() == only);       // uma so: nada acontece

    wm_maximize_toggle(only);
    wm_minimize(only);
    CHECK(wm_get_focused() == nullptr);
    wm_unminimize(only);
    CHECK(only->maximized && !only->minimized && wm_get_focused() == only);
    CHECK(same(only->bounds, 0, 0, 1024, 728));            // continua maximizada
    wm_minimize(only);
    wm_restore(only);                                       // API antiga: desmaximiza tambem
    CHECK(!only->maximized);

    printf("\n%d verificacoes, %d falha(s)\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
