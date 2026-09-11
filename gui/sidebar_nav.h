#pragma once
#include "widget.h"

class SidebarNav : public Widget {
public:
    typedef void (*SelectCallback)(int index, void* user_data);

    SidebarNav(int32_t x, int32_t y, uint32_t w, uint32_t h);

    // Adiciona uma categoria à lista, na ordem de inserção.
    // Retorna o índice da categoria (0-based), útil para o chamador
    // guardar e comparar depois em set_on_select.
    int add_item(const char* label);

    void draw(int32_t ox, int32_t oy) override;
    EventResult on_event(const WidgetEvent& ev) override;

    int  selected_index() const { return m_selected; }
    void set_selected(int index);

    void set_on_select(SelectCallback cb, void* user_data = nullptr) {
        m_on_select = cb;
        m_user_data = user_data;
    }

private:
    static constexpr int MAX_ITEMS = 8;
    static constexpr uint32_t ITEM_H = 34;

    char           m_labels[MAX_ITEMS][32];
    int            m_item_count;
    int            m_selected;
    SelectCallback m_on_select;
    void*          m_user_data;
};
