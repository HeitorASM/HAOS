#pragma once
#include "../../kernel/types.h"
#include "theme_abi.h"      // theme_generation / theme_export_abi (ABI para syscalls e apps)


struct Theme {
    // ---- Chrome de janela ----
    uint32_t window_bg;
    uint32_t titlebar_bg;
    uint32_t titlebar_fg;
    uint32_t window_border;
    uint32_t close_button;
    uint32_t shadow;

    // ---- Widgets de formulário ----
    uint32_t button_bg;
    uint32_t button_bg_hover;
    uint32_t button_bg_pressed;
    uint32_t button_fg;
    uint32_t button_border;
    uint32_t button_border_focused;

    uint32_t textfield_bg;
    uint32_t textfield_fg;
    uint32_t textfield_border;
    uint32_t textfield_border_focused;
    uint32_t textfield_cursor;

    uint32_t checkbox_bg;
    uint32_t checkbox_border;
    uint32_t checkbox_check;

    uint32_t label_fg;
    uint32_t label_fg_disabled;

    uint32_t listview_bg;
    uint32_t listview_item_bg;
    uint32_t listview_item_bg_selected;
    uint32_t listview_fg;

    uint32_t panel_bg;
    uint32_t panel_border;

    // ---- Métricas ----
    uint32_t titlebar_height;
    uint32_t border_width;
    uint32_t spacing_sm;
    uint32_t spacing_md;
    uint32_t spacing_lg;
    uint32_t widget_height;
    uint32_t corner_radius;

    // ============================================================
    // Personalização (Configurações -> Personalização)
    // ============================================================

    // ---- Cores semânticas ----
    // Substituem COLOR_TEXT_LIGHT / COLOR_TEXT_GRAY / COLOR_ACCENT
    // espalhados pelo código. Todo texto novo deve usar um destes
    // em vez de constante hardcoded, senão o tema fica "pela metade".
    uint32_t text_primary;   // texto principal sobre window_bg
    uint32_t text_secondary; // texto secundário (menos contraste)
    uint32_t text_dim;       // hints, rodapés, texto desabilitado
    uint32_t accent;         // destaque (borda de foco, indicador)
    uint32_t accent_dark;    // variação mais escura do accent

    // ---- Formas ----
    bool     window_rounded;
    uint32_t window_corner_radius;
    bool     buttons_rounded;
    uint32_t button_corner_radius;

    // ---- Sombra das janelas ----
    bool     window_shadow;

    // ---- Titlebar (variações por preset) ----
    uint32_t titlebar_bg_bottom;
    uint32_t titlebar_bg_unfocused;
    uint32_t titlebar_bg_unfocused_bottom;

    // ---- Taskbar ----
    uint32_t taskbar_bg_top;
    uint32_t taskbar_bg_bottom;
    uint32_t taskbar_line;
    uint32_t taskbar_text;
    uint32_t taskbar_item_bg;
    uint32_t taskbar_item_bg_focused;
    uint32_t taskbar_start_bg;

    // ---- Start menu ----
    uint32_t startmenu_bg_top;
    uint32_t startmenu_bg_bottom;
    uint32_t startmenu_border;
    uint32_t startmenu_header_top;
    uint32_t startmenu_header_bottom;
    uint32_t startmenu_line;
    uint32_t startmenu_item_bg;
    uint32_t startmenu_item_border;
    uint32_t startmenu_text;
    uint32_t startmenu_header_text;
    uint32_t startmenu_footer_text;

    // ---- Tela de login ----
    uint32_t login_card_border;
    uint32_t login_footer_bg;
    uint32_t login_footer_line;
    uint32_t login_lang_bg;
    uint32_t login_lang_bg_hover;
    uint32_t login_lang_bg_pressed;
    uint32_t login_lang_border;
    uint32_t login_lang_text;
    uint32_t login_lang_text_active;
    uint32_t login_lang_text_inactive;
};

// ---- Presets disponíveis ----
enum {
    THEME_PRESET_DEFAULT  = 0,
    THEME_PRESET_TERMINAL = 1,
    THEME_PRESET_XP       = 2,
    THEME_PRESET_BW       = 3,
    THEME_PRESET_CANDY    = 4,
    THEME_PRESET_COUNT
};

// ---- Cores de taskbar independentes ----
enum {
    TASKBAR_STYLE_AUTO  = 0,
    TASKBAR_STYLE_DARK  = 1,
    TASKBAR_STYLE_BLUE  = 2,
    TASKBAR_STYLE_PINK  = 3,
    TASKBAR_STYLE_GREEN = 4,
    TASKBAR_STYLE_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif

const Theme* theme_current(void);

// (legado) copia `new_theme` para o tema de trabalho.
const Theme* theme_set(const Theme* new_theme);

// Preset "Padrão" embutido.
const Theme* theme_default(void);

// ---- Presets ----
void theme_apply_preset(int preset);
int  theme_current_preset(void);

// ---- Toggles independentes ----
void theme_set_window_rounded(bool rounded);
void theme_set_buttons_rounded(bool rounded);
bool theme_get_window_rounded(void);
bool theme_get_buttons_rounded(void);

void theme_set_window_shadow(bool on);
bool theme_get_window_shadow(void);

// ---- Cor da taskbar ----
void theme_set_taskbar_style(int style);
int  theme_get_taskbar_style(void);

// ---- Persistência em /etc/haos.theme ----
void theme_load_preferences(void);
void theme_save_preferences(void);

#ifdef __cplusplus
}
#endif