#pragma once
#include "../kernel/types.h"

// ============================================================
//  widget.h — Hierarquia OOP da UI do HAOS
//
//  Prova de conceito do runtime C++ e da heap dinâmica:
//    - Widget     : classe base abstrata com draw() virtual puro
//    - Button     : widget concreto clicável
//    - Label      : widget concreto de texto estático
//    - Window2    : contêiner que gere uma lista de Widgets via 'new'
//
//  NÃO usa std::vector, std::list, std::string nem qualquer stdlib.
//  A lista de widgets é implementada via lista ligada simples.
// ============================================================

// ---- Estrutura de retângulo simples ----
struct Rect {
    int32_t  x, y;
    uint32_t w, h;

    bool contains(int32_t px, int32_t py) const {
        return px >= x && px < x + (int32_t)w &&
               py >= y && py < y + (int32_t)h;
    }
};

// ============================================================
//  Widget — Classe Base Abstrata
// ============================================================
class Widget {
public:
    Rect     bounds;    // posição e tamanho relativo ao pai
    bool     visible;   // é visível?
    bool     enabled;   // aceita input?

    Widget(int32_t x, int32_t y, uint32_t w, uint32_t h)
        : bounds{x, y, w, h}, visible(true), enabled(true) {}

    // Destrutor virtual — necessário para delete correto em herança
    virtual ~Widget() = default;

    // draw() é puro: cada subclasse DEVE implementar.
    // ox,oy = offset absoluto do pai no framebuffer
    virtual void draw(int32_t ox, int32_t oy) = 0;

    // on_click: chamado quando o widget é clicado (coordenadas relativas ao pai)
    virtual void on_click(int32_t /*mx*/, int32_t /*my*/) {}

    // on_key: chamado quando o widget tem foco e uma tecla é pressionada
    virtual void on_key(uint8_t /*c*/) {}
};

// ============================================================
//  Nó interno da lista ligada de Widgets
// ============================================================
struct WidgetNode {
    Widget*     widget;
    WidgetNode* next;
};

// ============================================================
//  WidgetList — Lista ligada de ponteiros Widget* (sem stdlib)
// ============================================================
class WidgetList {
public:
    WidgetList()  : m_head(nullptr), m_count(0) {}
    ~WidgetList() { clear(); }

    // Adiciona um widget à lista (assume ownership do ponteiro)
    void add(Widget* w);

    // Remove e destrói todos os widgets
    void clear();

    // Itera e chama draw em todos os widgets visíveis
    void draw_all(int32_t ox, int32_t oy);

    // Dispara on_click no primeiro widget que contém (mx,my)
    // Retorna true se algum widget consumiu o evento
    bool dispatch_click(int32_t ox, int32_t oy, int32_t mx, int32_t my);

    int count() const { return m_count; }

private:
    WidgetNode* m_head;
    int         m_count;
};

// ============================================================
//  Button — Widget concreto clicável
// ============================================================
class Button : public Widget {
public:
    char     label[32];
    uint32_t bg_color;
    uint32_t fg_color;
    uint32_t border_color;
    bool     pressed;

    // Callback opcional: chamado no on_click
    void (*on_click_cb)(Button* btn);

    Button(int32_t x, int32_t y, uint32_t w, uint32_t h,
           const char* lbl,
           uint32_t bg = 0x1A3A6A, uint32_t fg = 0xDDEEFF,
           uint32_t border = 0x5AA0FF);

    void draw(int32_t ox, int32_t oy) override;
    void on_click(int32_t mx, int32_t my) override;
};

// ============================================================
//  Label — Widget de texto estático
// ============================================================
class Label : public Widget {
public:
    char     text[64];
    uint32_t color;
    bool     transparent_bg;

    Label(int32_t x, int32_t y, const char* txt, uint32_t col = 0xDDEEFF);

    void draw(int32_t ox, int32_t oy) override;
    void set_text(const char* txt);
};

// ============================================================
//  Window2 — Janela OOP com lista de Widgets dinâmicos
//  (Nomeada Window2 para não colidir com a Window em C existente)
// ============================================================
class Window2 {
public:
    Rect       bounds;
    char       title[64];
    WidgetList widgets;   // lista encadeada de Widgets alocados via 'new'
    bool       active;
    bool       focused;

    Window2(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* t);
    virtual ~Window2() = default;

    // Adiciona um widget à janela (new é chamado pelo chamador; Window2 toma ownership)
    void add_widget(Widget* w) { widgets.add(w); }

    // Desenha a janela inteira (chrome + widgets)
    virtual void draw();

    // Distribui eventos
    void dispatch_click(int32_t mx, int32_t my);
    void dispatch_key(uint8_t c);

protected:
    // Ponto de extensão: subclasses podem sobrepor o conteúdo
    virtual void draw_content();

    // Widget com foco atual (para teclado)
    Widget* m_focused_widget;
};
