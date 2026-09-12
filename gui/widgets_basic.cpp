//  widgets_basic.cpp — Button, Label, Checkbox

#include "widgets_basic.h"
#include "../kernel/memory.h"
#include "../drivers/fb.h"

//  Button

Button::Button(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* label)
    : Widget(x, y, w, h), m_pressed(false), m_hovered(false), m_on_click(nullptr)
{
    kstrncpy(m_label, label, sizeof(m_label) - 1);
    m_label[sizeof(m_label) - 1] = '\0';
}

void Button::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    uint32_t bg = m_pressed ? t->button_bg_pressed
                : m_active_style ? t->button_bg_pressed  // mesmo tom do "pressed" para indicar seleção persistente
                : m_hovered ? t->button_bg_hover
                            : t->button_bg;
    uint32_t border = (focused || m_active_style) ? t->button_border_focused : t->button_border;

    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, bg);
    fb_draw_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, border, 1);
    fb_draw_string_centered((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h,
                            m_label, t->button_fg, 0, true);
}

EventResult Button::on_event(const WidgetEvent& ev) {
    switch (ev.type) {
        case EventType::MouseDown:
            m_pressed = true;
            return EventResult::Handled;

        case EventType::MouseUp:
            if (m_pressed) {
                m_pressed = false;
                // Só dispara o clique se o solte ainda estiver dentro
                // do próprio botão (dispatch() já garante isso, já
                // que MouseUp só chega aqui se (ev.x,ev.y) caiu no
                // bounds do widget).
                if (m_on_click) m_on_click(this);
            }
            return EventResult::Handled;

        case EventType::MouseMove:
            m_hovered = true;
            return EventResult::Handled;

        case EventType::Focus:
            focused = true;
            return EventResult::Handled;

        case EventType::Blur:
            focused = false;
            return EventResult::Handled;

        default:
            return EventResult::Ignored;
    }
}


//  Label


Label::Label(int32_t x, int32_t y, const char* text)
    : Widget(x, y, 0, 16)
{
    set_text(text);
}

void Label::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;
    uint32_t fg = enabled ? t->label_fg : t->label_fg_disabled;
    fb_draw_string((uint32_t)ax, (uint32_t)ay, m_text, fg, 0, true);
}

uint32_t Label::preferred_width() const {
    return fb_text_width(m_text);
}

void Label::set_text(const char* text) {
    kstrncpy(m_text, text, sizeof(m_text) - 1);
    m_text[sizeof(m_text) - 1] = '\0';
    bounds.w = fb_text_width(m_text);
}


//  Checkbox


Checkbox::Checkbox(int32_t x, int32_t y, const char* label, bool checked)
    : Widget(x, y, 0, BOX_SIZE), m_checked(checked), m_on_toggle(nullptr)
{
    kstrncpy(m_label, label, sizeof(m_label) - 1);
    m_label[sizeof(m_label) - 1] = '\0';
    bounds.w = BOX_SIZE + theme_current()->spacing_sm + fb_text_width(m_label);
}

void Checkbox::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    fb_fill_rect((uint32_t)ax, (uint32_t)ay, BOX_SIZE, BOX_SIZE, t->checkbox_bg);
    fb_draw_rect((uint32_t)ax, (uint32_t)ay, BOX_SIZE, BOX_SIZE, t->checkbox_border, 1);

    if (m_checked) {
        // Marcação simples: um "x" desenhado como retângulo menor
        // interno (evita depender de glifo específico de fonte).
        fb_fill_rect((uint32_t)(ax + 3), (uint32_t)(ay + 3),
                     BOX_SIZE - 6, BOX_SIZE - 6, t->checkbox_check);
    }

    fb_draw_string((uint32_t)(ax + BOX_SIZE + (int32_t)t->spacing_sm),
                   (uint32_t)(ay - 1), m_label, t->label_fg, 0, true);
}

EventResult Checkbox::on_event(const WidgetEvent& ev) {
    if (ev.type == EventType::MouseDown) {
        m_checked = !m_checked;
        if (m_on_toggle) m_on_toggle(this, m_checked);
        return EventResult::Handled;
    }
    return EventResult::Ignored;
}

// ScrollBar

ScrollBar::ScrollBar(int32_t x, int32_t y, uint32_t w, uint32_t h)
    : Widget(x, y, w, h),
      m_content_size(0), m_viewport_size(0), m_value(0), m_max_value(0),
      m_dragging(false), m_on_change(nullptr) {}

void ScrollBar::set_range(int content_size, int viewport_size) {
    m_content_size = content_size > 0 ? content_size : 0;
    m_viewport_size = viewport_size > 0 ? viewport_size : 0;
    m_max_value = m_content_size > m_viewport_size
                    ? m_content_size - m_viewport_size : 0;
    set_value(m_value);
}

void ScrollBar::set_value(int value) {
    if (value < 0) value = 0;
    if (value > m_max_value) value = m_max_value;
    if (value == m_value) return;
    m_value = value;
    if (m_on_change) m_on_change(this, m_value);
}

int ScrollBar::thumb_size() const {
    if (bounds.h == 0 || m_content_size <= 0) return 0;
    int size = (int)bounds.h * m_viewport_size / m_content_size;
    if (size < 16) size = 16;
    if (size > (int)bounds.h) size = (int)bounds.h;
    return size;
}

int ScrollBar::thumb_offset() const {
    int usable = (int)bounds.h - thumb_size();
    if (usable <= 0 || m_max_value <= 0) return 0;
    return usable * m_value / m_max_value;
}

void ScrollBar::draw(int32_t ox, int32_t oy) {
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;
    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, 0x101C34);
    if (m_max_value <= 0) return;

    int thumb = thumb_size();
    fb_fill_rect((uint32_t)ax, (uint32_t)(ay + thumb_offset()), bounds.w,
                 (uint32_t)thumb, 0x3A66A8);
}

void ScrollBar::update_from_y(int y) {
    int thumb = thumb_size();
    int usable = (int)bounds.h - thumb;
    if (usable <= 0 || m_max_value <= 0) return;

    int offset = y - thumb / 2;
    if (offset < 0) offset = 0;
    if (offset > usable) offset = usable;
    set_value(offset * m_max_value / usable);
}

EventResult ScrollBar::on_event(const WidgetEvent& ev) {
    if (ev.type == EventType::MouseDown) {
        if (ev.x < 0 || ev.x >= (int32_t)bounds.w ||
            ev.y < 0 || ev.y >= (int32_t)bounds.h)
            return EventResult::Ignored;
        m_dragging = true;
        update_from_y(ev.y);
        return EventResult::Handled;
    }
    if (ev.type == EventType::MouseDrag && m_dragging) {
        update_from_y(ev.y);
        return EventResult::Handled;
    }
    if (ev.type == EventType::MouseUp) {
        m_dragging = false;
        return EventResult::Handled;
    }
    if (ev.type == EventType::MouseScroll && ev.scroll != 0) {
        set_value(m_value - ev.scroll);
        return EventResult::Handled;
    }
    return EventResult::Ignored;
}
