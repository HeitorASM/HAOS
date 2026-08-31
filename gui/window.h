#pragma once
#include "container.h"

// ============================================================
//  window.h — Window do HAOS (v2)
//
//  Substitui a struct Window (C, callbacks fixos) e a classe
//  Window2 (C++, quase idêntica mas duplicada) por UMA única
//  classe Window, subclasse de Container — ou seja, ela já ganha
//  de graça: lista de filhos, despacho de evento, gerenciamento
//  de foco (Tab entre campos) do Container/Widget Core novo.
//
//  A aparência visual (sombra, gradiente na titlebar, botões
//  circulares fechar/maximizar/minimizar) é preservada
//  EXATAMENTE como estava em window.c — esta migração muda como
//  o código é organizado, não como o sistema parece na tela.
//
//  O WM (gerenciador de janelas — foco entre janelas, arrastar
//  pela titlebar, z-order) continua com a MESMA API pública que
//  window.h/window.c já tinham (wm_create, wm_focus,
//  wm_mouse_down/move/up, wm_draw_all, wm_dispatch_key) — assim
//  boot/welcome/desktop/terminal/editor/config/about precisam de
//  mudanças mínimas para migrar (etapa 5).
// ============================================================

enum class WinType : uint8_t {
    Terminal = 0,
    About,
    Dialog,
    Generic,
};

class Window : public Container {
public:
    Window(int32_t x, int32_t y, uint32_t w, uint32_t h,
          const char* title, WinType type = WinType::Generic);

    void draw(int32_t ox, int32_t oy) override;

    // ---- Estado gerenciado pelo WM (wm.cpp) ----
    bool     active;
    bool     minimized;
    WinType  type;
    char     title[64];

    // Estado de arrasto pela titlebar (gerenciado pelo WM)
    bool    dragging;
    int32_t drag_ox, drag_oy;

    // ---- Métricas de chrome (usadas pelo WM para hit-testing) ----
    static constexpr uint32_t TITLE_BAR_H = 30;
    static constexpr uint32_t BTN_SIZE    = 14;
    static constexpr uint32_t BTN_GAP     = 5;
    static constexpr uint32_t BORDER      = 2;

    // Área de conteúdo em coordenadas ABSOLUTAS de tela — usado por
    // apps que ainda desenham "manualmente" dentro da janela (ex.:
    // Terminal, que faz scroll de texto raw em vez de usar widgets).
    // Preservado para apps de terceiro migrarem gradualmente, sem
    // forçar todo mundo a reescrever para widgets imediatamente.
    Rect content_area_absolute() const;
};
