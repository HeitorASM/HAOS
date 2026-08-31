#pragma once
#include "widget.h"

// ============================================================
//  widgets_basic.h — Button, Label, Checkbox
//
//  Todos usam theme_current() para cor — nenhuma cor hardcoded
//  aqui. Trocar o tema ativo muda a aparência de todos sem
//  recompilar nenhum widget.
// ============================================================

//  Button
class Button : public Widget {
public:
    typedef void (*ClickCallback)(Button* self);

    Button(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* label);

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    void set_on_click(ClickCallback cb) { m_on_click = cb; }
    const char* label() const { return m_label; }

private:
    char          m_label[64];
    bool          m_pressed;
    bool          m_hovered;
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
