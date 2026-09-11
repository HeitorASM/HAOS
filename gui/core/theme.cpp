#include "theme.h"

static const Theme s_default_theme = {
    // ---- Chrome de janela ----
    .window_bg      = 0x0F1220,
    .titlebar_bg    = 0x1A3A6A,
    .titlebar_fg    = 0xDDEEFF,
    .window_border  = 0x2A4A8A,
    .close_button   = 0xE05050,
    .shadow         = 0x202020,

    // ---- Botão ----
    .button_bg              = 0x1A3A6A,
    .button_bg_hover        = 0x234A82,
    .button_bg_pressed      = 0x2A4A8A,
    .button_fg              = 0xDDEEFF,
    .button_border          = 0x2A4A8A,
    .button_border_focused  = 0x5A9AE8,

    // ---- Campo de texto ----
    .textfield_bg             = 0x0A0D18,
    .textfield_fg             = 0xDDEEFF,
    .textfield_border         = 0x2A4A8A,
    .textfield_border_focused = 0x5A9AE8,
    .textfield_cursor         = 0xDDEEFF,

    // ---- Checkbox ----
    .checkbox_bg     = 0x0A0D18,
    .checkbox_border = 0x2A4A8A,
    .checkbox_check  = 0x5A9AE8,

    // ---- Label ----
    .label_fg          = 0xDDEEFF,
    .label_fg_disabled = 0x6A7A9A,

    // ---- ListView ----
    .listview_bg               = 0x0A0D18,
    .listview_item_bg          = 0x0F1220,
    .listview_item_bg_selected = 0x1A3A6A,
    .listview_fg               = 0xDDEEFF,

    // ---- Panel ----
    .panel_bg     = 0x0F1220,
    .panel_border = 0x2A4A8A,

    // ---- Métricas ----
    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,
};

static const Theme* s_current_theme = &s_default_theme;

extern "C" {

const Theme* theme_current(void) {
    return s_current_theme;
}

const Theme* theme_set(const Theme* new_theme) {
    const Theme* old = s_current_theme;
    if (new_theme) s_current_theme = new_theme;
    return old;
}

const Theme* theme_default(void) {
    return &s_default_theme;
}

}
