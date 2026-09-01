#pragma once
#include "../../kernel/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void draw_start_menu(void);

// Testa se (mx, my) caiu em cima de algum item clicável do menu
// iniciar. Retorna o índice do item (0-based, na mesma ordem
// desenhada por draw_start_menu — separadores não contam como
// item), ou -1 se o clique não caiu em nenhum item.
int start_menu_hit_test(int32_t mx, int32_t my);

#ifdef __cplusplus
}
#endif
