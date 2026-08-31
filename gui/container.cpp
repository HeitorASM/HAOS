#include "container.h"
#include "../drivers/fb.h"

//  Container

void Container::draw(int32_t ox, int32_t oy) {
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;
    m_children.draw_all(ax, ay);
}

EventResult Container::on_event(const WidgetEvent& ev) {
    int32_t ax = bounds.x;
    int32_t ay = bounds.y;

    // Clique dentro do container: além de despachar para o filho
    // sob o cursor, atualiza qual filho tem o foco (só faz sentido
    // para MouseDown — o clique "decide" quem fica focado).
    if (ev.type == EventType::MouseDown) {
        WidgetNode* cur = m_children.head();
        while (cur) {
            Widget* w = cur->widget;
            if (w && w->visible && w->enabled &&
                w->bounds.contains(ev.x - ax, ev.y - ay)) {
                if (m_focused_child != w) {
                    if (m_focused_child) {
                        WidgetEvent blur{EventType::Blur, 0, 0, 0};
                        m_focused_child->on_event(blur);
                    }
                    m_focused_child = w;
                    WidgetEvent focus{EventType::Focus, 0, 0, 0};
                    w->on_event(focus);
                }
                break;
            }
            cur = cur->next;
        }
    }

    // KeyDown vai direto para o filho focado, sem checagem de
    // posição (não é um evento de mouse).
    if (ev.type == EventType::KeyDown) {
        if (m_focused_child) {
            return m_focused_child->on_event(ev);
        }
        return EventResult::Ignored;
    }

    return m_children.dispatch(ev, ax, ay);
}

bool Container::focus_next() {
    if (m_children.count() == 0) return false;

    WidgetNode* cur = m_children.head();
    bool found_current = (m_focused_child == nullptr);
    Widget* next_candidate = nullptr;

    while (cur) {
        if (found_current && cur->widget && cur->widget->visible && cur->widget->enabled) {
            next_candidate = cur->widget;
            break;
        }
        if (cur->widget == m_focused_child) found_current = true;
        cur = cur->next;
    }

    // Se não achou nenhum depois do atual, volta para o primeiro
    // focável (comportamento de "roda" o foco, como Tab costuma
    // fazer em qualquer sistema de UI).
    if (!next_candidate) {
        cur = m_children.head();
        while (cur) {
            if (cur->widget && cur->widget->visible && cur->widget->enabled) {
                next_candidate = cur->widget;
                break;
            }
            cur = cur->next;
        }
    }

    if (next_candidate && next_candidate != m_focused_child) {
        if (m_focused_child) {
            WidgetEvent blur{EventType::Blur, 0, 0, 0};
            m_focused_child->on_event(blur);
        }
        m_focused_child = next_candidate;
        WidgetEvent focus{EventType::Focus, 0, 0, 0};
        next_candidate->on_event(focus);
        return true;
    }
    return false;
}

//  Panel

void Panel::draw(int32_t ox, int32_t oy) {
    const Theme* t = theme_current();
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, t->panel_bg);
    if (m_draw_border) {
        fb_draw_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, t->panel_border, 1);
    }

    // Container::draw() desenha os filhos por cima do fundo do painel.
    Container::draw(ox, oy);
}

//  Canvas

void Canvas::draw(int32_t ox, int32_t oy) {
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;
    if (m_on_draw) {
        m_on_draw(this, ax, ay, bounds.w, bounds.h);
    }
}

EventResult Canvas::on_event(const WidgetEvent& ev) {
    if (m_on_event) {
        m_on_event(this, ev);
        return EventResult::Handled;
    }
    return EventResult::Ignored;
}
