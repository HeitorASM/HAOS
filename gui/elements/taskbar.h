#pragma once
#include "../../kernel/types.h"
#include "../window.h"

#define TASKBAR_H    40
#define START_BTN_W  100
#define CLOCK_W      72

// Larguras/posições dos itens de janela na taskbar — usados tanto
// para desenhar quanto para hit-test de clique (mantidos aqui para
// os dois lados ficarem sempre em sincronia).
#define TASKBAR_ITEM_W   140
#define TASKBAR_ITEM_GAP 6

#ifdef __cplusplus
extern "C" {
#endif

// Desenha a taskbar inteira: botão Iniciar, um item por JANELA
// ATIVA (não só a focada — antes só mostrava o terminal via um
// bool avulso), e o relógio.
void draw_taskbar(uint64_t ticks, bool start_menu_open);

// Testa se (mx,my) caiu em cima de algum item de janela na
// taskbar. Retorna a Window* correspondente, ou nullptr se o
// clique não caiu em nenhum item (ex.: caiu no botão Iniciar, no
// relógio, ou em espaço vazio da taskbar).
Window* taskbar_hit_test(int32_t mx, int32_t my);

#ifdef __cplusplus
}
#endif
