#pragma once
#include "widget.h"

// ============================================================
//  container.h — Container, Panel, Canvas
//
//  Container: classe base para qualquer Widget que tenha filhos
//  (Panel, e futuramente Window — ver etapa de migração do WM).
//  Cuida de: desenhar todos os filhos, despachar eventos de mouse
//  para o filho correto, e gerenciar qual filho tem o foco de
//  teclado (Tab avança o foco, clique também move o foco).
//
//  Canvas: widget "escape hatch" para quem quiser desenho livre
//  dentro de uma janela (visualizadores, jogos simples) sem passar
//  pelo sistema de widgets — só fornece uma callback de draw
//  customizada. É o ponto de encontro com o estilo "immediate mode"
//  que você viu no Nuklear, só que restrito à área do próprio
//  Canvas em vez de tomar conta da janela inteira.
// ============================================================

class Container : public Widget {
public:
    Container(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : Widget(x, y, w, h) {}

    void add(Widget* child) { m_children.add(child); }

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    // Move o foco de teclado para o próximo widget focável na lista
    // (chamado pelo WM ao receber Tab). Retorna true se algum
    // widget foi focado.
    bool focus_next();

protected:
    WidgetList m_children;
    Widget*    m_focused_child = nullptr;
};

// ============================================================
//  Panel — container visual simples, com fundo e borda opcionais.
//  Uso típico: agrupar um conjunto de campos de formulário com uma
//  moldura visível (ex.: seção "Rede" numa tela de configurações).
// ============================================================
class Panel : public Container {
public:
    Panel(int32_t x, int32_t y, uint32_t w, uint32_t h, bool draw_border = true)
        : Container(x, y, w, h), m_draw_border(draw_border) {}

    void draw(int32_t ox, int32_t oy) override;

private:
    bool m_draw_border;
};

// ============================================================
//  Canvas — área de desenho livre. Quem cria passa uma função de
//  callback que recebe as coordenadas absolutas de tela do canto
//  superior esquerdo do Canvas e o tamanho disponível — o dono do
//  Canvas desenha o que quiser dentro dessa área usando fb_* direto.
// ============================================================
class Canvas : public Widget {
public:
    typedef void (*DrawCallback)(Canvas* self, int32_t ax, int32_t ay,
                                 uint32_t w, uint32_t h);
    typedef void (*EventCallback)(Canvas* self, const WidgetEvent& ev);

    Canvas(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : Widget(x, y, w, h), m_on_draw(nullptr), m_on_event(nullptr) {}

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    void set_on_draw(DrawCallback cb)   { m_on_draw = cb; }
    void set_on_event(EventCallback cb) { m_on_event = cb; }

    // Espaço livre para o dono do Canvas guardar seu próprio estado
    // (ex.: um ponteiro para os dados que o visualizador desenha),
    // já que não há herança de Canvas esperada para o caso simples.
    void* user_data = nullptr;

private:
    DrawCallback  m_on_draw;
    EventCallback m_on_event;
};
