#pragma once
#include "../../kernel/types.h"

// ============================================================
//  gui/elements/menu.h — menus popup genéricos (contexto, submenus)
//
//  Uso típico (o chamador monta o menu a cada abertura):
//
//      menu_close();  menu_pool_reset();
//      Menu* m = menu_new();
//      menu_add(m, "Abrir", ID_OPEN);
//      menu_add(m, "Renomear", ID_RENAME, 0, "F2");
//      menu_add_separator(m);
//      Menu* sub = menu_add_submenu(m, "Novo");
//      menu_add(sub, "Pasta", ID_NEW_DIR);
//      menu_popup(m, mx, my, on_menu, ctx);
//
//  No loop de interface:
//      menu_update(mx, my);                 // todo frame (hover abre/fecha submenus)
//      if (clique esquerdo) if (menu_mouse_down(mx, my)) ...consumido
//      se uma tecla chegou: if (menu_is_open() && menu_key(k)) ...consumida
//      menu_draw();                         // por último, por cima de tudo
//
//  O callback roda FORA do desenho, no mesmo contexto do loop de interface,
//  então pode abrir janelas à vontade. Cada menu é desenhado com as cores
//  do tema atual.
// ============================================================

#define MENU_MAX_ITEMS     16   // por menu
#define MENU_MAX_MENUS     12   // menus + submenus vivos ao mesmo tempo
#define MENU_LABEL_MAX     40
#define MENU_SHORTCUT_MAX  14

enum {
    MENU_FLAG_DISABLED  = 1,   // aparece esmaecido e não responde
    MENU_FLAG_CHECKED   = 2,   // marcador à esquerda (item "ligado"/selecionado)
    MENU_FLAG_SEPARATOR = 4    // linha divisória (use menu_add_separator)
};

struct Menu;

struct MenuItem {
    char     label[MENU_LABEL_MAX];
    char     shortcut[MENU_SHORTCUT_MAX];   // texto à direita (só exibição), "" = nenhum
    int      id;                            // entregue ao callback
    uint8_t  flags;
    Menu*    submenu;                       // nullptr = item comum
};

struct Menu {
    MenuItem items[MENU_MAX_ITEMS];
    int      count;
};

typedef void (*MenuCallback)(int id, void* user);

// ---- Montagem (pool estático; nada de heap) ----
void  menu_pool_reset(void);                 // devolve todos os menus ao pool (feche o menu antes)
Menu* menu_new(void);                        // nullptr se o pool acabou
bool  menu_add(Menu* m, const char* label, int id,
               uint8_t flags = 0, const char* shortcut = nullptr);
bool  menu_add_separator(Menu* m);
Menu* menu_add_submenu(Menu* m, const char* label, uint8_t flags = 0);   // devolve o submenu

// ---- Uso ----
void  menu_popup(Menu* root, int32_t x, int32_t y, MenuCallback cb, void* user);
void  menu_close(void);
bool  menu_is_open(void);
bool  menu_contains(int32_t mx, int32_t my);   // o ponto está sobre algum nível aberto?
void  menu_update(int32_t mx, int32_t my);     // hover: abre/fecha submenus
bool  menu_mouse_down(int32_t mx, int32_t my); // true = clique consumido (dentro do menu);
                                               // false = foi fora: o menu fechou, deixe o clique seguir
bool  menu_key(uint8_t key);                   // setas/Enter/Esc; true = tecla consumida
void  menu_draw(void);
