//  widgets_input.cpp — TextField
#include "widgets_input.h"
#include "../kernel/memory.h"
#include "../kernel/keyboard.h"
#include "../drivers/fb.h"

TextField::TextField(int32_t x, int32_t y, uint32_t w, uint32_t h,
                     const char* placeholder)
    : Widget(x, y, w, h), m_length(0), m_cursor_pos(0)
{
    m_text[0] = '\0';
    kstrncpy(m_placeholder, placeholder, sizeof(m_placeholder) - 1);
    m_placeholder[sizeof(m_placeholder) - 1] = '\0';
}

void TextField::set_text(const char* text) {
    kstrncpy(m_text, text, sizeof(m_text) - 1);
    m_text[sizeof(m_text) - 1] = '\0';
    m_length = (uint32_t)kstrlen(m_text);
    m_cursor_pos = m_length;
}

void TextField::insert_char(uint8_t c) {
    if (m_length + 1 >= sizeof(m_text)) return; // sem espaço (+1 para o '\0')

    // Desloca tudo a partir do cursor uma posição para a direita,
    // de trás para frente para não sobrescrever antes de ler.
    for (uint32_t i = m_length; i > m_cursor_pos; i--) {
        m_text[i] = m_text[i - 1];
    }
    m_text[m_cursor_pos] = (char)c;
    m_length++;
    m_cursor_pos++;
    m_text[m_length] = '\0';
}

void TextField::backspace() {
    if (m_cursor_pos == 0) return;
    for (uint32_t i = m_cursor_pos - 1; i < m_length - 1; i++) {
        m_text[i] = m_text[i + 1];
    }
    m_length--;
    m_cursor_pos--;
    m_text[m_length] = '\0';
}

void TextField::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, t->textfield_bg);
    uint32_t border = focused ? t->textfield_border_focused : t->textfield_border;
    fb_draw_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, border, 1);

    int32_t text_x = ax + (int32_t)t->spacing_sm;
    int32_t text_y = ay + (int32_t)((bounds.h - 16) / 2);

    if (m_length == 0 && !focused) {
        // Placeholder — mesma cor "desabilitada" do tema, para
        // distinguir visualmente de texto real digitado.
        fb_draw_string((uint32_t)text_x, (uint32_t)text_y, m_placeholder,
                       t->label_fg_disabled, 0, true);
    } else {
        fb_draw_string((uint32_t)text_x, (uint32_t)text_y, m_text,
                       t->textfield_fg, 0, true);
    }

    // Cursor: só desenhado quando focado — uma barra vertical fina
    // na posição correspondente a m_cursor_pos dentro do texto.
    if (focused) {
        char before_cursor[128];
        for (uint32_t i = 0; i < m_cursor_pos; i++) before_cursor[i] = m_text[i];
        before_cursor[m_cursor_pos] = '\0';
        uint32_t cursor_x = text_x + fb_text_width(before_cursor);
        fb_fill_rect(cursor_x, (uint32_t)(text_y - 1), 2, 16, t->textfield_cursor);
    }
}

EventResult TextField::on_event(const WidgetEvent& ev) {
    switch (ev.type) {
        case EventType::MouseDown:
            // O Container pai é responsável por chamar Focus neste
            // widget e Blur no anteriormente focado (ver container.cpp)
            // — aqui só sinalizamos que o clique foi tratado.
            return EventResult::Handled;

        case EventType::Focus:
            focused = true;
            return EventResult::Handled;

        case EventType::Blur:
            focused = false;
            return EventResult::Handled;

        case EventType::KeyDown: {
            if (!focused) return EventResult::Ignored;
            uint8_t c = ev.key;
            if (c == 0x08) {                  // Backspace
                backspace();
            } else if (c == KEY_LEFT) {
                if (m_cursor_pos > 0) m_cursor_pos--;
            } else if (c == KEY_RIGHT) {
                if (m_cursor_pos < m_length) m_cursor_pos++;
            } else if (c == 0x0A || c == 0x0D) {
                // Enter — TextField de uma linha não insere quebra;
                // quem quiser tratar "submeter" faz isso observando
                // este evento em uma subclasse ou callback futuro.
            } else if (c >= 0x20 && c < 0x7F) { // ASCII imprimível
                insert_char(c);
            }
            return EventResult::Handled;
        }

        default:
            return EventResult::Ignored;
    }
}
