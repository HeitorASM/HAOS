#pragma once
#include "types.h"

// ============================================================
//  libc/include/haos/uitheme.h — cores e métricas do TEMA do sistema
//
//      ui_theme_t th;
//      ui_theme_load(&th);                       // lê o tema atual
//      cv_fill(&cv, 0, 0, w, h, th.c[UI_WINDOW_BG]);
//
//  Quando o usuário troca o tema, o kernel manda HAOS_EV_THEME para as janelas
//  do programa: chame ui_theme_load() de novo e redesenhe. ui_theme_changed()
//  diz se a "geração" mudou desde a última leitura.
//
//  Os índices ESPELHAM theme_export_abi() em gui/core/theme.cpp — mantenha a ordem.
// ============================================================

enum {
    UI_THEME_GENERATION = 0,   // muda sempre que o tema muda (não é cor)
    UI_WINDOW_BG,              // 1  fundo das janelas
    UI_PANEL_BG,               // 2  painéis
    UI_PANEL_BORDER,           // 3
    UI_TEXT,                   // 4  texto principal
    UI_TEXT_SECONDARY,         // 5
    UI_TEXT_DIM,               // 6  dicas / desabilitado
    UI_ACCENT,                 // 7  destaque
    UI_ACCENT_DARK,            // 8
    UI_BUTTON_BG,              // 9
    UI_BUTTON_HOVER,           // 10
    UI_BUTTON_PRESSED,         // 11
    UI_BUTTON_FG,              // 12
    UI_BUTTON_BORDER,          // 13
    UI_BUTTON_BORDER_FOCUS,    // 14
    UI_FIELD_BG,               // 15 campos de texto
    UI_FIELD_FG,               // 16
    UI_FIELD_BORDER,           // 17
    UI_FIELD_BORDER_FOCUS,     // 18
    UI_FIELD_CURSOR,           // 19
    UI_CHECK_BG,               // 20
    UI_CHECK_BORDER,           // 21
    UI_CHECK_MARK,             // 22
    UI_LIST_BG,                // 23
    UI_LIST_ITEM,              // 24
    UI_LIST_SELECTED,          // 25
    UI_LIST_FG,                // 26
    UI_BUTTONS_ROUNDED,        // 27 (0/1)
    UI_BUTTON_RADIUS,          // 28
    UI_SPACING_SM,             // 29
    UI_SPACING_MD,             // 30
    UI_SPACING_LG,             // 31
    UI_WIDGET_HEIGHT,          // 32
    UI_THEME_COUNT
};

typedef struct {
    uint32_t c[UI_THEME_COUNT];
} ui_theme_t;

// Lê o tema atual do kernel. Retorna 0, ou -1 se a syscall falhar (nesse caso
// `t` recebe um tema escuro padrão, para o programa continuar usável).
int  ui_theme_load(ui_theme_t* t);

// 1 se o tema do sistema mudou desde a última ui_theme_load() (consulta o kernel).
int  ui_theme_changed(void);
