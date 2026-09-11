#include "sidebar_nav.h"
#include "../kernel/memory.h"
#include "../drivers/fb.h"

SidebarNav::SidebarNav(int32_t x, int32_t y, uint32_t w, uint32_t h)
    : Widget(x, y, w, h), m_item_count(0), m_selected(0),
      m_on_select(nullptr), m_user_data(nullptr)
{}

int SidebarNav::add_item(const char* label) {
    if (m_item_count >= MAX_ITEMS) return -1;
    int idx = m_item_count++;
    kstrncpy(m_labels[idx], label, sizeof(m_labels[idx]) - 1);
    m_labels[idx][sizeof(m_labels[idx]) - 1] = '\0';
    return idx;
}

void SidebarNav::set_selected(int index) {
    if (index < 0 || index >= m_item_count) return;
    m_selected = index;
    if (m_on_select) m_on_select(index, m_user_data);
}

void SidebarNav::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, t->listview_bg);

    for (int i = 0; i < m_item_count; i++) {
        int32_t iy = ay + (int32_t)(i * ITEM_H);
        bool sel = (i == m_selected);

        uint32_t bg = sel ? t->listview_item_bg_selected : t->listview_item_bg;
        fb_fill_rect((uint32_t)ax, (uint32_t)iy, bounds.w, ITEM_H - 2, bg);

        if (sel) {
            // Barra de destaque à esquerda do item selecionado — o
            // mesmo indicador visual usado na taskbar para a janela
            // focada, mantendo consistência entre os dois widgets.
            fb_fill_rect((uint32_t)ax, (uint32_t)iy, 3, ITEM_H - 2, COLOR_ACCENT);
        }

        fb_draw_string((uint32_t)(ax + 14), (uint32_t)(iy + (int32_t)(ITEM_H - 16) / 2),
                       m_labels[i], t->listview_fg, 0, true);
    }
}

EventResult SidebarNav::on_event(const WidgetEvent& ev) {
    if (ev.type != EventType::MouseDown) return EventResult::Ignored;

    int clicked = (int)(ev.y / (int32_t)ITEM_H);
    if (clicked >= 0 && clicked < m_item_count) {
        set_selected(clicked);
        return EventResult::Handled;
    }
    return EventResult::Ignored;
}
