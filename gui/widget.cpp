// ============================================================
//  widget.cpp — Implementação da hierarquia OOP de UI do HAOS
//
//  Os operadores new/delete utilizados aqui delegam para a
//  KernelHeap definida em memory.cpp — sem stdlib, sem malloc.
// ============================================================

#include "widget.h"
#include "../kernel/memory.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"

// ============================================================
//  WidgetList
// ============================================================

void WidgetList::add(Widget* w) {
    if (!w) return;
    // Aloca um nó via new (→ KernelHeap)
    WidgetNode* node = new WidgetNode;
    node->widget = w;
    node->next   = nullptr;

    // Insere no final da lista
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
        delete cur->widget;  // destrói o widget (chama destrutor virtual)
        delete cur;          // liberta o nó
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

bool WidgetList::dispatch_click(int32_t ox, int32_t oy, int32_t mx, int32_t my) {
    WidgetNode* cur = m_head;
    while (cur) {
        Widget* w = cur->widget;
        if (w && w->visible && w->enabled) {
            // Converte coordenadas absolutas para relativas ao widget
            int32_t rel_x = mx - ox - w->bounds.x;
            int32_t rel_y = my - oy - w->bounds.y;
            if (w->bounds.contains(mx - ox, my - oy)) {
                w->on_click(rel_x, rel_y);
                return true;
            }
        }
        cur = cur->next;
    }
    return false;
}

// ============================================================
//  Button
// ============================================================

Button::Button(int32_t x, int32_t y, uint32_t w, uint32_t h,
               const char* lbl, uint32_t bg, uint32_t fg, uint32_t border)
    : Widget(x, y, w, h),
      bg_color(bg), fg_color(fg), border_color(border),
      pressed(false), on_click_cb(nullptr)
{
    // Copia a label (sem strncpy da stdlib)
    kstrncpy(label, lbl, sizeof(label) - 1);
    label[sizeof(label)-1] = '\0';
}

void Button::draw(int32_t ox, int32_t oy) {
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;

    // Fundo do botão
    uint32_t bg = pressed ? border_color : bg_color;
    fb_fill_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, bg);

    // Borda
    fb_draw_rect((uint32_t)ax, (uint32_t)ay, bounds.w, bounds.h, border_color, 1);

    // Texto centrado
    uint32_t tw = fb_text_width(label);
    int32_t  tx = ax + (int32_t)((bounds.w - tw) / 2);
    int32_t  ty = ay + (int32_t)((bounds.h - 16) / 2);  // 16 = altura do font
    if (tx < ax) tx = ax + 4;
    if (ty < ay) ty = ay + 2;

    fb_draw_string((uint32_t)tx, (uint32_t)ty, label, fg_color, 0, true);
}

void Button::on_click(int32_t mx, int32_t my) {
    (void)mx; (void)my;
    if (on_click_cb) on_click_cb(this);
}

// ============================================================
//  Label
// ============================================================

Label::Label(int32_t x, int32_t y, const char* txt, uint32_t col)
    : Widget(x, y, 0, 16), color(col), transparent_bg(true)
{
    kstrncpy(text, txt, sizeof(text) - 1);
    text[sizeof(text)-1] = '\0';
    // Calcula largura automaticamente
    bounds.w = fb_text_width(text);
}

void Label::draw(int32_t ox, int32_t oy) {
    int32_t ax = ox + bounds.x;
    int32_t ay = oy + bounds.y;
    fb_draw_string((uint32_t)ax, (uint32_t)ay, text, color, 0, transparent_bg);
}

void Label::set_text(const char* txt) {
    kstrncpy(text, txt, sizeof(text) - 1);
    text[sizeof(text)-1] = '\0';
    bounds.w = fb_text_width(text);
}

// ============================================================
//  Window2
// ============================================================

// Cores do chrome da janela (mesmo esquema visual do projeto)
static constexpr uint32_t W2_CHROME_BG     = 0x0F1220;
static constexpr uint32_t W2_TITLE_BG      = 0x1A3A6A;
static constexpr uint32_t W2_TITLE_FG      = 0xDDEEFF;
static constexpr uint32_t W2_BORDER        = 0x2A4A8A;
static constexpr uint32_t W2_CLOSE_COL     = 0xE05050;
static constexpr uint32_t W2_TITLE_H       = 30;
static constexpr uint32_t W2_BORDER_W      = 2;

Window2::Window2(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* t)
    : bounds{x, y, w, h}, active(true), focused(false),
      m_focused_widget(nullptr)
{
    kstrncpy(title, t, sizeof(title) - 1);
    title[sizeof(title)-1] = '\0';
}

void Window2::draw() {
    int32_t  x = bounds.x, y = bounds.y;
    uint32_t w = bounds.w, h = bounds.h;

    // ---- Sombra ----
    fb_fill_rect((uint32_t)(x + 4), (uint32_t)(y + 4), w, h, 0x00000080 & 0x202020);

    // ---- Fundo geral ----
    fb_fill_rect((uint32_t)x, (uint32_t)y, w, h, W2_CHROME_BG);

    // ---- Barra de título ----
    fb_fill_rect((uint32_t)x, (uint32_t)y, w, W2_TITLE_H, W2_TITLE_BG);

    // Texto do título
    fb_draw_string((uint32_t)(x + 10), (uint32_t)(y + 8),
                   title, W2_TITLE_FG, 0, true);

    // Botão fechar (X)
    uint32_t btn_x = (uint32_t)(x + (int32_t)w - 22);
    uint32_t btn_y = (uint32_t)(y + 8);
    fb_fill_rect(btn_x, btn_y, 14, 14, W2_CLOSE_COL);
    fb_draw_string(btn_x + 3, btn_y + 1, "x", 0xFFFFFF, 0, true);

    // ---- Borda ----
    fb_draw_rect((uint32_t)x, (uint32_t)y, w, h, W2_BORDER, W2_BORDER_W);

    // ---- Conteúdo (widgets) ----
    draw_content();
}

void Window2::draw_content() {
    // Offset do conteúdo (abaixo do título, dentro da borda)
    int32_t cx = bounds.x + W2_BORDER_W;
    int32_t cy = bounds.y + W2_TITLE_H;
    widgets.draw_all(cx, cy);
}

void Window2::dispatch_click(int32_t mx, int32_t my) {
    int32_t cx = bounds.x + (int32_t)W2_BORDER_W;
    int32_t cy = bounds.y + (int32_t)W2_TITLE_H;
    widgets.dispatch_click(cx, cy, mx, my);
}

void Window2::dispatch_key(uint8_t c) {
    if (m_focused_widget) m_focused_widget->on_key(c);
}
