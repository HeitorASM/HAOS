#pragma once
#include "core/widget.h"

//  widgets_basic.h — Button, Label, Checkbox

//  Button

class Button : public Widget {
public:
    typedef void (*ClickCallback)(Button* self);

    Button(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* label);

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    void set_on_click(ClickCallback cb) { m_on_click = cb; }
    const char* label() const { return m_label; }

    // Marca o botão como "ativo/selecionado" visualmente (borda de
    // destaque, mesmo estilo do foco por teclado) — útil para botões
    // usados como seletor de opção (ex.: idioma, modo de exibição),
    // onde o app quer indicar qual opção está atualmente escolhida
    // sem que o botão precise estar de fato com o foco de teclado.
    void set_active_style(bool active) { m_active_style = active; }

    // Tag numérica livre para o app associar a este botão (ex.: o
    // índice de um item numa lista dinâmica de botões, como o
    // wallpaper selecionado). O callback de clique (ClickCallback)
    // só recebe o próprio Button* — sem isso, não haveria como saber
    // "qual" botão específico foi clicado quando vários botões
    // idênticos em estrutura compartilham a mesma função de callback
    // (funções livres em C++ não capturam contexto por closure).
    // Uso típico: button->set_tag(i); ... no callback: self->tag()
    void set_tag(int tag) { m_tag = tag; }
    int  tag() const { return m_tag; }

private:
    char          m_label[64];
    bool          m_pressed;
    bool          m_hovered;
    bool          m_active_style = false;
    int           m_tag = -1;
    ClickCallback m_on_click;
};


//  Label

class Label : public Widget {
public:
    Label(int32_t x, int32_t y, const char* text);

    void draw(int32_t ox, int32_t oy) override;
    uint32_t preferred_width()  const override;
    uint32_t preferred_height() const override { return 16; }

    void set_text(const char* text);
    const char* text() const { return m_text; }

private:
    char m_text[128];
};


//  Checkbox

class Checkbox : public Widget {
public:
    typedef void (*ToggleCallback)(Checkbox* self, bool checked);

    Checkbox(int32_t x, int32_t y, const char* label, bool checked = false);

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    bool is_checked() const { return m_checked; }
    void set_checked(bool c) { m_checked = c; }
    void set_on_toggle(ToggleCallback cb) { m_on_toggle = cb; }

private:
    char           m_label[64];
    bool           m_checked;
    ToggleCallback m_on_toggle;

    static constexpr uint32_t BOX_SIZE = 14;
};

// Barra vertical reutilizavel para conteudo maior que a area visivel.
class ScrollBar : public Widget {
public:
    static constexpr uint32_t DEFAULT_WIDTH = 10;
    typedef void (*ChangeCallback)(ScrollBar* self, int value);

    ScrollBar(int32_t x, int32_t y, uint32_t w, uint32_t h);

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    void set_range(int content_size, int viewport_size);
    void set_value(int value);
    int value() const { return m_value; }
    int max_value() const { return m_max_value; }
    void set_on_change(ChangeCallback cb) { m_on_change = cb; }

private:
    int m_content_size;
    int m_viewport_size;
    int m_value;
    int m_max_value;
    bool m_dragging;
    ChangeCallback m_on_change;

    int thumb_size() const;
    int thumb_offset() const;
    void update_from_y(int y);
};
