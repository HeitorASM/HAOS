#pragma once
#include "window.h"

// ============================================================
//  wm.h — Window Manager do HAOS
//
//  Mesma API pública que window.h (versão antiga) já expunha —
//  wm_create/focus/close/mouse_*/draw_all/dispatch_key — para que
//  boot/welcome/desktop/terminal/editor/config/about precisem de
//  mudanças mínimas ao migrar (principalmente: WinType agora é
//  "enum class", e Window* aponta para a classe C++ nova em vez
//  da struct C antiga).
// ============================================================

#define MAX_WINDOWS 8

void    wm_init(void);
Window* wm_create(WinType type, int32_t x, int32_t y,
                  uint32_t w, uint32_t h, const char* title);

// Registra uma janela já construída externamente (ex.: uma
// subclasse própria como AboutWindow, criada com `new` fora do
// wm_create genérico) na lista do WM, para que ganhe foco/z-order/
// fechar como qualquer outra. Retorna false se não houver slot
// livre (a janela NÃO é deletada nesse caso — quem chamou decide
// o que fazer).
bool    wm_register(Window* win);

void    wm_close(Window* win);
void    wm_focus(Window* win);
Window* wm_get_focused(void);
int     wm_active_count(void);

// ---- Minimizar / Maximizar / Restaurar ----
// Estas funções existem tanto para uso interno do WM (hit-test dos
// botões de chrome) quanto para a taskbar poder oferecer as mesmas
// ações a partir da lista de janelas (ex.: clicar no item da taskbar
// de uma janela minimizada deve restaurá-la e focá-la).
void    wm_minimize(Window* win);
void    wm_restore(Window* win);     // desminimiza E desmaximiza
void    wm_maximize_toggle(Window* win);

// ---- Iteração de todas as janelas (para a taskbar listar todas,
//      não só a focada) ----
// Percorre os MAX_WINDOWS slots; retorna nullptr em slots vazios.
// A taskbar deve pular nullptr e janelas com active == false.
Window* wm_get_window_at(int index);
int     wm_window_slot_count(void); // sempre MAX_WINDOWS, para uso em loops

void    wm_draw_window(Window* win);
void    wm_draw_all(void);

void    wm_dispatch_key(uint8_t c);

// Mouse events — retorna true se o evento foi consumido por
// alguma janela (usado pelo desktop.c para decidir se o clique
// "vazou" para ícones/taskbar por baixo).
bool    wm_mouse_down(int32_t mx, int32_t my);
void    wm_mouse_move(int32_t mx, int32_t my);
void    wm_mouse_up(void);
