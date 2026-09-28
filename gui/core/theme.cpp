#include "theme.h"
#include "../../fs/vfs.h"
#include "../../kernel/memory.h"

// ============================================================
//  Presets embutidos
// ============================================================

// ---- Padrão (azul escuro original do HAOS) ----
static const Theme s_default_theme = {
    .window_bg      = 0x0F1220,
    .titlebar_bg    = 0x1A3A6A,
    .titlebar_fg    = 0xDDEEFF,
    .window_border  = 0x2A4A8A,
    .close_button   = 0xE05050,
    .shadow         = 0x202020,

    .button_bg              = 0x1A3A6A,
    .button_bg_hover        = 0x234A82,
    .button_bg_pressed      = 0x2A4A8A,
    .button_fg              = 0xDDEEFF,
    .button_border          = 0x2A4A8A,
    .button_border_focused  = 0x5A9AE8,

    .textfield_bg             = 0x0A0D18,
    .textfield_fg             = 0xDDEEFF,
    .textfield_border         = 0x2A4A8A,
    .textfield_border_focused = 0x5A9AE8,
    .textfield_cursor         = 0xDDEEFF,

    .checkbox_bg     = 0x0A0D18,
    .checkbox_border = 0x2A4A8A,
    .checkbox_check  = 0x5A9AE8,

    .label_fg          = 0xDDEEFF,
    .label_fg_disabled = 0x6A7A9A,

    .listview_bg               = 0x0A0D18,
    .listview_item_bg          = 0x0F1220,
    .listview_item_bg_selected = 0x1A3A6A,
    .listview_fg               = 0xDDEEFF,

    .panel_bg     = 0x0F1220,
    .panel_border = 0x2A4A8A,

    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,

    .text_primary   = 0xDDEEFF,
    .text_secondary = 0x99AACC,
    .text_dim       = 0x6A7A9A,
    .accent         = 0x5A9AE8,
    .accent_dark    = 0x3A6AB8,

    .window_rounded        = true,
    .window_corner_radius  = 6,
    .buttons_rounded       = false,
    .button_corner_radius  = 0,
    .window_shadow         = true,

    .titlebar_bg_bottom           = 0x102450,
    .titlebar_bg_unfocused        = 0x282838,
    .titlebar_bg_unfocused_bottom = 0x181828,

    .taskbar_bg_top          = 0x0C1020,
    .taskbar_bg_bottom       = 0x060810,
    .taskbar_line            = 0x2A4A7A,
    .taskbar_text            = 0xDDEEFF,
    .taskbar_item_bg         = 0x0E1626,
    .taskbar_item_bg_focused = 0x1E3E78,
    .taskbar_start_bg        = 0x101E38,

    .startmenu_bg_top           = 0x0F2648,
    .startmenu_bg_bottom        = 0x0A1220,
    .startmenu_border           = 0x0C1220,
    .startmenu_header_top       = 0x0F2648,
    .startmenu_header_bottom    = 0x0A1220,
    .startmenu_line             = 0x223A66,
    .startmenu_item_bg          = 0x0E1830,
    .startmenu_item_border      = 0x1E3560,
    .startmenu_text             = 0xDDEEFF,
    .startmenu_header_text      = 0xDDEEFF,
    .startmenu_footer_text      = 0x6A7A9A,

    .login_card_border         = 0x14264A,
    .login_footer_bg           = 0x060810,
    .login_footer_line         = 0x2A4A7A,
    .login_lang_bg             = 0x172B4D,
    .login_lang_bg_hover       = 0x284D80,
    .login_lang_bg_pressed     = 0x315E9A,
    .login_lang_border         = 0x5D8FD5,
    .login_lang_text           = 0xD5E7FF,
    .login_lang_text_active    = 0xFFFFFF,
    .login_lang_text_inactive  = 0x8FA8C8,
};

// ---- Terminal (verde-fósforo, cantos retos) ----
static const Theme s_terminal_theme = {
    .window_bg      = 0x000A00,
    .titlebar_bg    = 0x002800,
    .titlebar_fg    = 0x30FF30,
    .window_border  = 0x00AA00,
    .close_button   = 0x00AA00,
    .shadow         = 0x000000,

    .button_bg              = 0x001800,
    .button_bg_hover        = 0x003000,
    .button_bg_pressed      = 0x005000,
    .button_fg              = 0x30FF30,
    .button_border          = 0x00AA00,
    .button_border_focused  = 0x30FF30,

    .textfield_bg             = 0x000A00,
    .textfield_fg             = 0x30FF30,
    .textfield_border         = 0x00AA00,
    .textfield_border_focused = 0x30FF30,
    .textfield_cursor         = 0x30FF30,

    .checkbox_bg     = 0x000A00,
    .checkbox_border = 0x00AA00,
    .checkbox_check  = 0x30FF30,

    .label_fg          = 0x30FF30,
    .label_fg_disabled = 0x1A5A1A,

    .listview_bg               = 0x000A00,
    .listview_item_bg          = 0x001000,
    .listview_item_bg_selected = 0x004000,
    .listview_fg               = 0x30FF30,

    .panel_bg     = 0x000A00,
    .panel_border = 0x00AA00,

    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,

    .text_primary   = 0x30FF30,
    .text_secondary = 0x20B020,
    .text_dim       = 0x1A5A1A,
    .accent         = 0x30FF30,
    .accent_dark    = 0x00AA00,

    .window_rounded        = false,
    .window_corner_radius  = 0,
    .buttons_rounded       = false,
    .button_corner_radius  = 0,
    .window_shadow         = false,  // terminal = visual limpo, sem sombra

    .titlebar_bg_bottom           = 0x000A00,
    .titlebar_bg_unfocused        = 0x002000,
    .titlebar_bg_unfocused_bottom = 0x000800,

    .taskbar_bg_top          = 0x001000,
    .taskbar_bg_bottom       = 0x000000,
    .taskbar_line            = 0x00AA00,
    .taskbar_text            = 0x30FF30,
    .taskbar_item_bg         = 0x001800,
    .taskbar_item_bg_focused = 0x005000,
    .taskbar_start_bg        = 0x002800,

    .startmenu_bg_top           = 0x001800,
    .startmenu_bg_bottom        = 0x000000,
    .startmenu_border           = 0x00AA00,
    .startmenu_header_top       = 0x003000,
    .startmenu_header_bottom    = 0x001000,
    .startmenu_line             = 0x00AA00,
    .startmenu_item_bg          = 0x001800,
    .startmenu_item_border      = 0x00AA00,
    .startmenu_text             = 0x30FF30,
    .startmenu_header_text      = 0x30FF30,
    .startmenu_footer_text      = 0x1A5A1A,

    .login_card_border         = 0x00AA00,
    .login_footer_bg           = 0x000000,
    .login_footer_line         = 0x00AA00,
    .login_lang_bg             = 0x002800,
    .login_lang_bg_hover       = 0x004000,
    .login_lang_bg_pressed     = 0x005000,
    .login_lang_border         = 0x00AA00,
    .login_lang_text           = 0x30FF30,
    .login_lang_text_active    = 0x30FF30,
    .login_lang_text_inactive  = 0x1A5A1A,
};

// ---- XP / Retro (azul clássico; janela do menu Iniciar é creme claro) ----
static const Theme s_xp_theme = {
    .window_bg      = 0xD4D0C8,
    .titlebar_bg    = 0x0058E8,
    .titlebar_fg    = 0xFFFFFF,
    .window_border  = 0x0058E8,
    .close_button   = 0xC84020,
    .shadow         = 0x404040,

    .button_bg              = 0xECE9D8,
    .button_bg_hover        = 0xF8F4E4,
    .button_bg_pressed      = 0xD8D4C0,
    .button_fg              = 0x000000,
    .button_border          = 0x7A7A6A,
    .button_border_focused  = 0x0058E8,

    .textfield_bg             = 0xFFFFFF,
    .textfield_fg             = 0x000000,
    .textfield_border         = 0x7A7A6A,
    .textfield_border_focused = 0x0058E8,
    .textfield_cursor         = 0x000000,

    .checkbox_bg     = 0xFFFFFF,
    .checkbox_border = 0x7A7A6A,
    .checkbox_check  = 0x0058E8,

    .label_fg          = 0x000000,
    .label_fg_disabled = 0x808080,

    .listview_bg               = 0xFFFFFF,
    .listview_item_bg          = 0xF0F0F0,
    .listview_item_bg_selected = 0x316AC5,
    .listview_fg               = 0x000000,

    .panel_bg     = 0xECE9D8,
    .panel_border = 0x7A7A6A,

    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,

    .text_primary   = 0x000000,
    .text_secondary = 0x404040,
    .text_dim       = 0x707070,
    .accent         = 0x0058E8,
    .accent_dark    = 0x003A9A,

    .window_rounded        = true,
    .window_corner_radius  = 8,
    .buttons_rounded       = true,
    .button_corner_radius  = 3,
    .window_shadow         = true,

    .titlebar_bg_bottom           = 0x3A9BFF,
    .titlebar_bg_unfocused        = 0x7A96B0,
    .titlebar_bg_unfocused_bottom = 0xB0C4D8,

    .taskbar_bg_top          = 0x245EDB,
    .taskbar_bg_bottom       = 0x1941A5,
    .taskbar_line            = 0x1030A0,
    .taskbar_text            = 0xFFFFFF,
    .taskbar_item_bg         = 0x3A8BFF,
    .taskbar_item_bg_focused = 0x1E5BB8,
    .taskbar_start_bg        = 0x3A9B48,

    // Menu Iniciar do XP: creme claro com header azul, texto PRETO
    // sobre o corpo. É isso que evita o problema de "texto branco
    // sobre fundo creme" que tinha antes.
    .startmenu_bg_top           = 0xFFFFFF,
    .startmenu_bg_bottom        = 0xECE9D8,
    .startmenu_border           = 0x0058E8,
    .startmenu_header_top       = 0x0058E8,
    .startmenu_header_bottom    = 0x3A9BFF,
    .startmenu_line             = 0xA0A0A0,
    .startmenu_item_bg          = 0xF0F0F0,
    .startmenu_item_border      = 0xD0D0D0,
    .startmenu_text             = 0x000000,
    .startmenu_header_text      = 0xFFFFFF,
    .startmenu_footer_text      = 0x404040,

    .login_card_border         = 0x0058E8,
    .login_footer_bg           = 0x245EDB,
    .login_footer_line         = 0x1030A0,
    .login_lang_bg             = 0x3A8BFF,
    .login_lang_bg_hover       = 0x4A9BFF,
    .login_lang_bg_pressed     = 0x1E5BB8,
    .login_lang_border         = 0x1030A0,
    .login_lang_text           = 0xFFFFFF,
    .login_lang_text_active    = 0xFFFF00,
    .login_lang_text_inactive  = 0xD0E0FF,
};

// ---- Preto & Branco ----
static const Theme s_bw_theme = {
    .window_bg      = 0x000000,
    .titlebar_bg    = 0x202020,
    .titlebar_fg    = 0xFFFFFF,
    .window_border  = 0xFFFFFF,
    .close_button   = 0xFFFFFF,
    .shadow         = 0x303030,

    .button_bg              = 0x000000,
    .button_bg_hover        = 0x202020,
    .button_bg_pressed      = 0x404040,
    .button_fg              = 0xFFFFFF,
    .button_border          = 0xFFFFFF,
    .button_border_focused  = 0xFFFFFF,

    .textfield_bg             = 0x000000,
    .textfield_fg             = 0xFFFFFF,
    .textfield_border         = 0xFFFFFF,
    .textfield_border_focused = 0xFFFFFF,
    .textfield_cursor         = 0xFFFFFF,

    .checkbox_bg     = 0x000000,
    .checkbox_border = 0xFFFFFF,
    .checkbox_check  = 0xFFFFFF,

    .label_fg          = 0xFFFFFF,
    .label_fg_disabled = 0x808080,

    .listview_bg               = 0x000000,
    .listview_item_bg          = 0x101010,
    .listview_item_bg_selected = 0x404040,
    .listview_fg               = 0xFFFFFF,

    .panel_bg     = 0x000000,
    .panel_border = 0xFFFFFF,

    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,

    .text_primary   = 0xFFFFFF,
    .text_secondary = 0xCCCCCC,
    .text_dim       = 0x808080,
    .accent         = 0xFFFFFF,
    .accent_dark    = 0xC0C0C0,

    .window_rounded        = false,
    .window_corner_radius  = 0,
    .buttons_rounded       = false,
    .button_corner_radius  = 0,
    .window_shadow         = true,

    .titlebar_bg_bottom           = 0x000000,
    .titlebar_bg_unfocused        = 0x181818,
    .titlebar_bg_unfocused_bottom = 0x000000,

    .taskbar_bg_top          = 0x101010,
    .taskbar_bg_bottom       = 0x000000,
    .taskbar_line            = 0xFFFFFF,
    .taskbar_text            = 0xFFFFFF,
    .taskbar_item_bg         = 0x181818,
    .taskbar_item_bg_focused = 0x404040,
    .taskbar_start_bg        = 0x202020,

    .startmenu_bg_top           = 0x101010,
    .startmenu_bg_bottom        = 0x000000,
    .startmenu_border           = 0xFFFFFF,
    .startmenu_header_top       = 0x202020,
    .startmenu_header_bottom    = 0x101010,
    .startmenu_line             = 0xFFFFFF,
    .startmenu_item_bg          = 0x181818,
    .startmenu_item_border      = 0x808080,
    .startmenu_text             = 0xFFFFFF,
    .startmenu_header_text      = 0xFFFFFF,
    .startmenu_footer_text      = 0x808080,

    .login_card_border         = 0xFFFFFF,
    .login_footer_bg           = 0x000000,
    .login_footer_line         = 0xFFFFFF,
    .login_lang_bg             = 0x202020,
    .login_lang_bg_hover       = 0x303030,
    .login_lang_bg_pressed     = 0x404040,
    .login_lang_border         = 0xFFFFFF,
    .login_lang_text           = 0xFFFFFF,
    .login_lang_text_active    = 0xFFFFFF,
    .login_lang_text_inactive  = 0x808080,
};

// ---- Algodão Doce (rosa/branco, bem arredondado) ----
static const Theme s_candy_theme = {
    .window_bg      = 0xFFE8F4,
    .titlebar_bg    = 0xFF90C0,
    .titlebar_fg    = 0xFFFFFF,
    .window_border  = 0xFF60A0,
    .close_button   = 0xFF5080,
    .shadow         = 0xC080A0,

    .button_bg              = 0xFFC8E0,
    .button_bg_hover        = 0xFFD8EA,
    .button_bg_pressed      = 0xFFB0D0,
    .button_fg              = 0x803060,
    .button_border          = 0xFF90C0,
    .button_border_focused  = 0xFF60A0,

    .textfield_bg             = 0xFFFFFF,
    .textfield_fg             = 0x803060,
    .textfield_border         = 0xFF90C0,
    .textfield_border_focused = 0xFF60A0,
    .textfield_cursor         = 0xFF60A0,

    .checkbox_bg     = 0xFFFFFF,
    .checkbox_border = 0xFF90C0,
    .checkbox_check  = 0xFF60A0,

    .label_fg          = 0x803060,
    .label_fg_disabled = 0xC0A0B0,

    .listview_bg               = 0xFFFFFF,
    .listview_item_bg          = 0xFFF0F8,
    .listview_item_bg_selected = 0xFFD0E8,
    .listview_fg               = 0x803060,

    .panel_bg     = 0xFFF0F8,
    .panel_border = 0xFFB0D0,

    .titlebar_height = 30,
    .border_width    = 2,
    .spacing_sm      = 4,
    .spacing_md      = 8,
    .spacing_lg      = 16,
    .widget_height   = 26,
    .corner_radius   = 0,

    .text_primary   = 0x803060,
    .text_secondary = 0xA06080,
    .text_dim       = 0xC080A0,
    .accent         = 0xFF60A0,
    .accent_dark    = 0xD04080,

    .window_rounded        = true,
    .window_corner_radius  = 12,
    .buttons_rounded       = true,
    .button_corner_radius  = 8,
    .window_shadow         = true,

    .titlebar_bg_bottom           = 0xFFB0D0,
    .titlebar_bg_unfocused        = 0xE0C0D0,
    .titlebar_bg_unfocused_bottom = 0xC0A0B0,

    .taskbar_bg_top          = 0xFFC0E0,
    .taskbar_bg_bottom       = 0xFF90C0,
    .taskbar_line            = 0xFF60A0,
    .taskbar_text            = 0x803060,
    .taskbar_item_bg         = 0xFFE0F0,
    .taskbar_item_bg_focused = 0xFFA0D0,
    .taskbar_start_bg        = 0xFF70B0,

    // Menu Iniciar: fundo rosa claro, texto marrom-avermelhado.
    .startmenu_bg_top           = 0xFFF0F8,
    .startmenu_bg_bottom        = 0xFFD0E8,
    .startmenu_border           = 0xFF90C0,
    .startmenu_header_top       = 0xFF90C0,
    .startmenu_header_bottom    = 0xFFB0D0,
    .startmenu_line             = 0xFF90C0,
    .startmenu_item_bg          = 0xFFE8F4,
    .startmenu_item_border      = 0xFFB0D0,
    .startmenu_text             = 0x803060,
    .startmenu_header_text      = 0xFFFFFF,
    .startmenu_footer_text      = 0xA06080,

    .login_card_border         = 0xFF90C0,
    .login_footer_bg           = 0xFF90C0,
    .login_footer_line         = 0xFF60A0,
    .login_lang_bg             = 0xFFD0E8,
    .login_lang_bg_hover       = 0xFFC0DC,
    .login_lang_bg_pressed     = 0xFFA0D0,
    .login_lang_border         = 0xFF60A0,
    .login_lang_text           = 0x803060,
    .login_lang_text_active    = 0x803060,
    .login_lang_text_inactive  = 0xC080A0,
};

// ============================================================
//  Estado interno
// ============================================================

namespace {

const Theme* s_presets[THEME_PRESET_COUNT] = {
    &s_default_theme,
    &s_terminal_theme,
    &s_xp_theme,
    &s_bw_theme,
    &s_candy_theme,
};

static Theme s_working;
static bool  s_working_valid = false;

static int  s_current_preset = THEME_PRESET_DEFAULT;
static int  s_taskbar_style  = TASKBAR_STYLE_AUTO;

void apply_taskbar_style(Theme* t, int style) {
    switch (style) {
        case TASKBAR_STYLE_DARK:
            t->taskbar_bg_top          = 0x080808;
            t->taskbar_bg_bottom       = 0x000000;
            t->taskbar_line            = 0x333333;
            t->taskbar_item_bg         = 0x141414;
            t->taskbar_item_bg_focused = 0x303030;
            t->taskbar_start_bg        = 0x1A1A1A;
            break;
        case TASKBAR_STYLE_BLUE:
            t->taskbar_bg_top          = 0x102A50;
            t->taskbar_bg_bottom       = 0x081A38;
            t->taskbar_line            = 0x2A4A7A;
            t->taskbar_item_bg         = 0x0E1E3A;
            t->taskbar_item_bg_focused = 0x1E3E78;
            t->taskbar_start_bg        = 0x163A6A;
            break;
        case TASKBAR_STYLE_PINK:
            t->taskbar_bg_top          = 0x5A2848;
            t->taskbar_bg_bottom       = 0x3A1830;
            t->taskbar_line            = 0xA06080;
            t->taskbar_item_bg         = 0x482038;
            t->taskbar_item_bg_focused = 0x78385A;
            t->taskbar_start_bg        = 0x6A3054;
            break;
        case TASKBAR_STYLE_GREEN:
            t->taskbar_bg_top          = 0x1A3A20;
            t->taskbar_bg_bottom       = 0x0A2010;
            t->taskbar_line            = 0x3A6A4A;
            t->taskbar_item_bg         = 0x14301A;
            t->taskbar_item_bg_focused = 0x2A5A38;
            t->taskbar_start_bg        = 0x1E4828;
            break;
        default:
            break;
    }
}

void ensure_working() {
    if (s_working_valid) return;
    s_working = *s_presets[s_current_preset];
    apply_taskbar_style(&s_working, s_taskbar_style);
    s_working_valid = true;
}

} // namespace

// ============================================================
//  API pública
// ============================================================

extern "C" {

const Theme* theme_current(void) {
    ensure_working();
    return &s_working;
}

const Theme* theme_set(const Theme* new_theme) {
    const Theme* old = theme_current();
    if (new_theme) {
        ensure_working();
        s_working = *new_theme;
    }
    return old;
}

const Theme* theme_default(void) {
    return &s_default_theme;
}

void theme_apply_preset(int preset) {
    if (preset < 0 || preset >= THEME_PRESET_COUNT) return;
    s_current_preset = preset;
    s_working_valid  = false;
    ensure_working();
    theme_save_preferences();
}

int theme_current_preset(void) {
    return s_current_preset;
}

void theme_set_window_rounded(bool rounded) {
    ensure_working();
    s_working.window_rounded = rounded;
    if (rounded && s_working.window_corner_radius == 0) {
        s_working.window_corner_radius = 6;
    }
    theme_save_preferences();
}

void theme_set_buttons_rounded(bool rounded) {
    ensure_working();
    s_working.buttons_rounded = rounded;
    if (rounded && s_working.button_corner_radius == 0) {
        s_working.button_corner_radius = 4;
    }
    theme_save_preferences();
}

bool theme_get_window_rounded(void) {
    ensure_working();
    return s_working.window_rounded;
}

bool theme_get_buttons_rounded(void) {
    ensure_working();
    return s_working.buttons_rounded;
}

void theme_set_window_shadow(bool on) {
    ensure_working();
    s_working.window_shadow = on;
    theme_save_preferences();
}

bool theme_get_window_shadow(void) {
    ensure_working();
    return s_working.window_shadow;
}

void theme_set_taskbar_style(int style) {
    if (style < 0 || style >= TASKBAR_STYLE_COUNT) return;
    s_taskbar_style = style;
    s_working_valid = false;
    ensure_working();
    theme_save_preferences();
}

int theme_get_taskbar_style(void) {
    return s_taskbar_style;
}

// ---- Persistência -------------------------------------------------

static VfsNode* theme_prefs_file(void) {
    VfsNode* file = vfs_resolve(vfs_root(), "/etc/haos.theme");
    if (file) return file;

    VfsNode* etc = vfs_resolve(vfs_root(), "/etc");
    return etc ? vfs_touch(etc, "haos.theme") : (VfsNode*)0;
}

static int parse_line_int(const char* text, int fallback) {
    int value = 0;
    bool has_digit = false;
    bool negative = false;
    if (*text == '-') { negative = true; text++; }
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        has_digit = true;
        text++;
    }
    if (!has_digit) return fallback;
    return negative ? -value : value;
}

static const char* next_line(const char* text) {
    while (*text && *text != '\n') text++;
    if (*text == '\n') text++;
    return text;
}

void theme_load_preferences(void) {
    VfsNode* file = vfs_resolve(vfs_root(), "/etc/haos.theme");
    if (!file || file->type != VFS_FILE || file->size == 0) return;

    const char* text = file->data;
    int preset  = parse_line_int(text, THEME_PRESET_DEFAULT); text = next_line(text);
    int taskbar = parse_line_int(text, TASKBAR_STYLE_AUTO);   text = next_line(text);
    int win_round  = parse_line_int(text, -1);                text = next_line(text);
    int btn_round  = parse_line_int(text, -1);                text = next_line(text);
    int win_shadow = parse_line_int(text, -1);

    if (preset  < 0 || preset  >= THEME_PRESET_COUNT)  preset  = THEME_PRESET_DEFAULT;
    if (taskbar < 0 || taskbar >= TASKBAR_STYLE_COUNT) taskbar = TASKBAR_STYLE_AUTO;

    s_current_preset = preset;
    s_taskbar_style  = taskbar;
    s_working_valid  = false;
    ensure_working();

    if (win_round == 0 || win_round == 1)
        s_working.window_rounded = (win_round != 0);
    if (btn_round == 0 || btn_round == 1)
        s_working.buttons_rounded = (btn_round != 0);
    if (win_shadow == 0 || win_shadow == 1)
        s_working.window_shadow = (win_shadow != 0);
}

void theme_save_preferences(void) {
    VfsNode* file = theme_prefs_file();
    if (!file) return;

    char buf[96];
    int  i = 0;
    char num[12];

    kitoa(s_current_preset, num);
    for (int k = 0; num[k]; k++) buf[i++] = num[k];
    buf[i++] = '\n';

    kitoa(s_taskbar_style, num);
    for (int k = 0; num[k]; k++) buf[i++] = num[k];
    buf[i++] = '\n';

    buf[i++] = s_working.window_rounded  ? '1' : '0'; buf[i++] = '\n';
    buf[i++] = s_working.buttons_rounded ? '1' : '0'; buf[i++] = '\n';
    buf[i++] = s_working.window_shadow   ? '1' : '0'; buf[i++] = '\n';
    buf[i]   = '\0';

    vfs_write(file, buf);
}

} // extern "C"