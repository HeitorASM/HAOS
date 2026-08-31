#pragma once
#include "widget.h"

// ============================================================
//  widgets_input.h — TextField
//
//  Campo de texto de uma linha, com cursor e edição básica
//  (inserir, backspace, mover cursor). Recebe foco via clique
//  ou Tab (gerenciado pelo Container pai) e passa a consumir
//  KeyDown enquanto focused == true.
// ============================================================

class TextField : public Widget {
public:
    TextField(int32_t x, int32_t y, uint32_t w, uint32_t h,
              const char* placeholder = "");

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    const char* text() const { return m_text; }
    void set_text(const char* text);

private:
    char     m_text[128];
    char     m_placeholder[64];
    uint32_t m_length;
    uint32_t m_cursor_pos; // índice do caractere onde o cursor está

    void insert_char(uint8_t c);
    void backspace();
};
