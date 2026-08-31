#include "window.h"
#include "../kernel/memory.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"

Window::Window(int32_t x, int32_t y, uint32_t w, uint32_t h,
              const char* title_, WinType type_)
    : Container(x, y, w, h),
      active(true), minimized(false), maximized(false), type(type_),
      dragging(false), drag_ox(0), drag_oy(0),
      resizing(false), resize_edge(0)
{
    kstrncpy(title, title_, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    saved_bounds = bounds;
}

void Window::on_resized() {
    // Implementação padrão: apenas notifica os filhos (útil para
    // Container widgets que usam layout automático). Apps que
    // desenham manualmente (Terminal, Editor) sobrescrevem isto se
    // precisarem recalcular algo além do que draw() já resolve via
    // content_area_absolute() sendo recalculado a cada frame.
    WidgetEvent ev{EventType::Resize, 0, 0, 0};
    m_children.dispatch(ev, 0, 0);
}

Rect Window::content_area_absolute() const {
    return Rect{
        bounds.x + (int32_t)BORDER,
        bounds.y + (int32_t)BORDER + (int32_t)TITLE_BAR_H + 1,
        bounds.w - BORDER * 2,
        bounds.h - BORDER - TITLE_BAR_H - 1,
    };
}

void Window::draw(int32_t ox, int32_t oy) {
    if (!active || minimized) return;

    uint32_t x = (uint32_t)(ox + bounds.x);
    uint32_t y = (uint32_t)(oy + bounds.y);
    uint32_t w = bounds.w, h = bounds.h;

    // --- Sombra (6px offset, stipple) ---
    fb_draw_shadow(x + 6, y + 6, w, h);

    // --- Borda externa (arredondada, 1px) ---
    uint32_t border_col = focused ? COLOR_WIN_BORDER_A : COLOR_WIN_BORDER;
    fb_draw_rounded_rect(x, y, w, h, border_col, 6);

    // --- Barra de título: gradiente do azul escuro ao médio ---
    uint32_t tc1 = focused ? 0x224A8C : 0x282838;
    uint32_t tc2 = focused ? 0x102450 : 0x181828;
    fb_fill_gradient_v(x + BORDER, y + BORDER,
                       w - BORDER * 2, TITLE_BAR_H, tc1, tc2);

    // Linha brilhante no topo da titlebar
    uint32_t shine = focused ? 0x4A80D0 : 0x3A3A50;
    fb_fill_rect(x + BORDER, y + BORDER, w - BORDER * 2, 1, shine);

    // --- Título ---
    uint32_t title_col = focused ? COLOR_TEXT_LIGHT : COLOR_TEXT_GRAY;
    uint32_t tx = x + BORDER + BTN_SIZE * 3 + BTN_GAP * 3 + 8;
    uint32_t ty = y + BORDER + (TITLE_BAR_H - FONT_H) / 2;
    fb_draw_string(tx, ty, title, title_col, 0, true);

    // --- Botões de controle (círculos com ícone) ---
    int32_t by2 = (int32_t)(y + BORDER) + (int32_t)(TITLE_BAR_H - BTN_SIZE) / 2;
    // Fechar (vermelho, "x")
    int32_t bx2 = (int32_t)(x + w - BORDER - BTN_SIZE - BTN_GAP);
    fb_fill_circle((uint32_t)(bx2 + (int32_t)BTN_SIZE / 2),
                   (uint32_t)(by2 + (int32_t)BTN_SIZE / 2), BTN_SIZE / 2, 0xE05555);
    fb_draw_string((uint32_t)(bx2 + 4), (uint32_t)(by2 + 3), "x", 0xFFAAAA, 0, true);

    // Maximizar (verde, quadrado vazado — ou dois quadrados
    // sobrepostos quando já maximizada, sinalizando "restaurar")
    bx2 -= (int32_t)(BTN_SIZE + BTN_GAP);
    fb_fill_circle((uint32_t)(bx2 + (int32_t)BTN_SIZE / 2),
                   (uint32_t)(by2 + (int32_t)BTN_SIZE / 2), BTN_SIZE / 2, 0x50B050);
    {
        int32_t icon_cx = bx2 + (int32_t)BTN_SIZE / 2;
        int32_t icon_cy = by2 + (int32_t)BTN_SIZE / 2;
        if (maximized) {
            // Ícone de "restaurar": dois quadrados pequenos sobrepostos
            fb_draw_rect((uint32_t)(icon_cx - 3), (uint32_t)(icon_cy - 3), 4, 4, 0x0A2A0A, 1);
            fb_draw_rect((uint32_t)(icon_cx - 1), (uint32_t)(icon_cy - 1), 4, 4, 0x0A2A0A, 1);
        } else {
            // Ícone de "maximizar": um quadrado vazado
            fb_draw_rect((uint32_t)(icon_cx - 3), (uint32_t)(icon_cy - 3), 6, 6, 0x0A2A0A, 1);
        }
    }

    // Minimizar (amarelo, traço horizontal)
    bx2 -= (int32_t)(BTN_SIZE + BTN_GAP);
    fb_fill_circle((uint32_t)(bx2 + (int32_t)BTN_SIZE / 2),
                   (uint32_t)(by2 + (int32_t)BTN_SIZE / 2), BTN_SIZE / 2, 0xE0B840);
    fb_fill_rect((uint32_t)(bx2 + (int32_t)BTN_SIZE / 2 - 3),
                (uint32_t)(by2 + (int32_t)BTN_SIZE / 2), 6, 1, 0x3A2A0A);

    // --- Separador abaixo da titlebar ---
    fb_fill_rect(x + BORDER, y + BORDER + TITLE_BAR_H, w - BORDER * 2, 1, 0x0A1428);

    // --- Área de conteúdo ---
    uint32_t cy  = y + BORDER + TITLE_BAR_H + 1;
    uint32_t cw  = w - BORDER * 2;
    uint32_t ch  = h - BORDER - TITLE_BAR_H - 1;
    uint32_t cbg = (type == WinType::Terminal) ? COLOR_TERM_BG : COLOR_WIN_BG;
    fb_fill_rect(x + BORDER, cy, cw, ch, cbg);

    // Widgets filhos (se algum foi adicionado via add()) desenhados
    // por cima do fundo do conteúdo. Apps que ainda desenham
    // "manualmente" (Terminal) simplesmente não adicionam filhos e
    // usam content_area_absolute() para saber onde desenhar.
    Container::draw(ox, oy);

    // --- Grip de redimensionamento (canto inferior direito) ---
    // Só desenhado quando focada e não maximizada — sinaliza ao
    // usuário que a borda/canto pode ser arrastado para redimensionar.
    if (focused && !maximized) {
        uint32_t gx = x + w - 10, gy = y + h - 10;
        uint32_t grip_col = 0x3A5AAA;
        for (int i = 0; i < 3; i++) {
            fb_fill_rect(gx + (uint32_t)(i * 3), gy + 8, 2, 2, grip_col);
            fb_fill_rect(gx + 8, gy + (uint32_t)(i * 3), 2, 2, grip_col);
        }
    }
}
