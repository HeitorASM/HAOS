#pragma once
#include "../kernel/types.h"


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
    uint32_t button_border_focused; // contorno quando selecionado via Tab

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

    // ---- Métricas (espaçamento consistente em toda a GUI) ----
    uint32_t titlebar_height;
    uint32_t border_width;
    uint32_t spacing_sm;    // espaçamento pequeno (ex.: entre label e campo)
    uint32_t spacing_md;    // espaçamento padrão entre widgets num layout
    uint32_t spacing_lg;    // espaçamento grande (ex.: entre seções)
    uint32_t widget_height; // altura padrão de botão/campo de texto
    uint32_t corner_radius; // reservado para quando fb.h suportar cantos arredondados
};

#ifdef __cplusplus
extern "C" {
#endif

// Retorna o tema atualmente ativo. Todo widget deve chamar isto em
// vez de usar cores hardcoded — é o único ponto de acesso ao tema.
const Theme* theme_current(void);

// Define um novo tema ativo (usado no futuro pelo sistema de
// "sabores"). Retorna o tema anterior, para permitir restaurar.
const Theme* theme_set(const Theme* new_theme);

// O tema padrão do HAOS (mesma paleta usada até agora no projeto:
// azul escuro, consistente com Window2/boot/welcome existentes).
const Theme* theme_default(void);

#ifdef __cplusplus
}
#endif
