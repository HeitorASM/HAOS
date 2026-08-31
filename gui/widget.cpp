//  widget.cpp — Implementação do Widget Core (v2)

#include "widget.h"
#include "../kernel/memory.h"

//  WidgetList

void WidgetList::add(Widget* w) {
    if (!w) return;
    WidgetNode* node = new WidgetNode;
    node->widget = w;
    node->next   = nullptr;

    if (!m_head) {
        m_head = node;
    } else {
        WidgetNode* cur = m_head;
        while (cur->next) cur = cur->next;
        cur->next = node;
    }
    m_count++;
}

void WidgetList::clear() {
    WidgetNode* cur = m_head;
    while (cur) {
        WidgetNode* next = cur->next;
        delete cur->widget;
        delete cur;
        cur = next;
    }
    m_head  = nullptr;
    m_count = 0;
}

void WidgetList::draw_all(int32_t ox, int32_t oy) {
    WidgetNode* cur = m_head;
    while (cur) {
        if (cur->widget && cur->widget->visible)
            cur->widget->draw(ox, oy);
        cur = cur->next;
    }
}

EventResult WidgetList::dispatch(const WidgetEvent& ev, int32_t ox, int32_t oy) {
    WidgetNode* cur = m_head;
    while (cur) {
        Widget* w = cur->widget;
        if (w && w->visible && w->enabled) {
            // Eventos de mouse só vão para o widget se as coordenadas
            // absolutas caírem dentro do seu bounds. Eventos que não
            // são de mouse (KeyDown, Focus, Blur, Resize) são
            // despachados sem checagem de posição — quem chama
            // dispatch() já decidiu qual widget deveria recebê-los
            // (ex.: o widget com foco).
            bool is_mouse = ev.type == EventType::MouseDown ||
                            ev.type == EventType::MouseUp   ||
                            ev.type == EventType::MouseMove ||
                            ev.type == EventType::MouseDrag;

            if (!is_mouse || w->bounds.contains(ev.x - ox, ev.y - oy)) {
                WidgetEvent local = ev;
                if (is_mouse) {
                    // Converte para coordenadas relativas ao próprio widget
                    local.x = ev.x - ox - w->bounds.x;
                    local.y = ev.y - oy - w->bounds.y;
                }
                if (w->on_event(local) == EventResult::Handled) {
                    return EventResult::Handled;
                }
            }
        }
        cur = cur->next;
    }
    return EventResult::Ignored;
}
