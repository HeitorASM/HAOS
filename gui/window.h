#pragma once
#include "container.h"

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
    bool     maximized;
    WinType  type;
    char     title[64];

    // Estado de arrasto pela titlebar (gerenciado pelo WM)
    bool    dragging;
    int32_t drag_ox, drag_oy;

    // Estado de redimensionamento (arrastar borda/canto)
    bool    resizing;
    uint8_t resize_edge; // combinação de RESIZE_* abaixo, 0 = não redimensionando

    // bounds salvos antes de maximizar, para restaurar ao desmaximizar
    Rect saved_bounds;

    // Tamanho mínimo que o WM respeita ao redimensionar (evita
    // encolher a janela até um estado inutilizável ou até bounds
    // negativos).
    uint32_t min_width  = 160;
    uint32_t min_height = 100;

    // ---- Métricas de chrome (usadas pelo WM para hit-testing) ----
    static constexpr uint32_t TITLE_BAR_H  = 30;
    static constexpr uint32_t BTN_SIZE     = 14;
    static constexpr uint32_t BTN_GAP      = 5;
    static constexpr uint32_t BORDER       = 2;
    static constexpr uint32_t RESIZE_GRIP  = 6; // espessura da faixa sensível a resize nas bordas

    // Bits de borda ativa durante um resize (podem combinar para
    // cantos, ex.: RESIZE_RIGHT | RESIZE_BOTTOM = canto inferior direito)
    static constexpr uint8_t RESIZE_LEFT   = 1 << 0;
    static constexpr uint8_t RESIZE_RIGHT  = 1 << 1;
    static constexpr uint8_t RESIZE_TOP    = 1 << 2;
    static constexpr uint8_t RESIZE_BOTTOM = 1 << 3;

    // Área de conteúdo em coordenadas ABSOLUTAS de tela — usado por
    // apps que ainda desenham "manualmente" dentro da janela (ex.:
    // Terminal, que faz scroll de texto raw em vez de usar widgets).
    // Preservado para apps de terceiro migrarem gradualmente, sem
    // forçar todo mundo a reescrever para widgets imediatamente.
    Rect content_area_absolute() const;

    // Chamado sempre que bounds muda de tamanho (maximizar, restaurar,
    // resize manual). Widgets/apps que precisam recalcular layout ou
    // reposicionar conteúdo interno (ex.: Terminal recalculando
    // colunas visíveis) podem sobrescrever isto. Implementação padrão
    // não faz nada — dispara um WidgetEvent::Resize para os filhos.
    virtual void on_resized();
};
