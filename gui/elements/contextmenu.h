#pragma once
#include "../../kernel/types.h"

// Menu de contexto do DESKTOP (botão direito sobre a área livre).
// Mesmo visual do menu Iniciar (usa o tema atual) e as mesmas strings
// traduzidas dos itens correspondentes.
//
// Itens, em ordem (o índice devolvido por contextmenu_hit_test):
//   0 Terminal   1 Editor   2 Arquivos   3 Configurações   4 Sobre

#define CONTEXTMENU_ITEM_COUNT 5

#ifdef __cplusplus
extern "C" {
#endif

// Abre o menu com o canto superior esquerdo em (x, y); a posição é
// ajustada para o menu caber inteiro na área acima da taskbar.
void contextmenu_open(int32_t x, int32_t y);
void contextmenu_close(void);
bool contextmenu_is_open(void);

// true se (mx, my) está em qualquer parte do menu (itens ou moldura).
bool contextmenu_contains(int32_t mx, int32_t my);

// Índice do item sob (mx, my), ou -1 (fora do menu / na moldura).
int  contextmenu_hit_test(int32_t mx, int32_t my);

// Desenha o menu, destacando o item sob o cursor. Não faz nada se
// estiver fechado.
void draw_contextmenu(int32_t mouse_x, int32_t mouse_y);

#ifdef __cplusplus
}
#endif
