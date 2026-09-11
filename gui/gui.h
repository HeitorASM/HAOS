#pragma once
#include "../kernel/types.h"

// ============================================================
//  gui.h — Ponto de entrada único do subsistema GUI do HAOS
//
//  Aplicativos e telas (gui/apps, gui/screens, gui/elements) e
//  código futuro devem incluir APENAS "gui/gui.h" — este header
//  "engole" o motor de janelas/widgets (gui/core/) para que quem
//  usa a GUI não precise saber que widget.h, container.h, window.h,
//  wm.h, layout.h e theme.h existem separadamente.
//
//  O motor (Widget/Container/Window/...) é C++ (classes). Alguns
//  arquivos C puro (ex.: kernel.c) incluem gui.h só para chamar
//  gui_init()/gui_run() via extern "C" — para esses, os includes
//  do motor C++ abaixo são pulados (não fariam sentido em C mesmo).
// ============================================================
#ifdef __cplusplus
#include "core/widget.h"
#include "core/container.h"
#include "core/window.h"
#include "core/wm.h"
#include "core/layout.h"
#include "core/theme.h"
#include "widgets_basic.h"
#include "widgets_input.h"
#include "sidebar_nav.h"
#endif

typedef enum {
    GUI_STATE_BOOT    = 0,
    GUI_STATE_WELCOME = 1,
    GUI_STATE_DESKTOP = 2,
} GuiState;

#ifdef __cplusplus
extern "C" {
#endif

void gui_init(void);
void gui_run(void);      // loop principal 

#ifdef __cplusplus
}
#endif