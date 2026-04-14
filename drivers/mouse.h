#pragma once
#include "../kernel/types.h"

// ---- PS/2 Mouse driver ----------------------------------------
// Inicializa o mouse PS/2 (habilita porta auxiliar + reporting)
void    mouse_init(void);

// Processa bytes pendentes do FIFO de ISR (chamar no loop principal)
void    mouse_process(void);

// Snapshot de estado (chamar 1x por frame, antes de ler clicks)
void    mouse_snap(void);

// Define limites da tela para clamping do cursor
void    mouse_set_bounds(int32_t max_x, int32_t max_y);

// Posição atual do cursor
int32_t mouse_get_x(void);
int32_t mouse_get_y(void);

// Estado de botões (após mouse_snap)
bool    mouse_left_pressed(void);   // botão esquerdo está pressionado
bool    mouse_left_clicked(void);   // foi pressionado NESTE frame
bool    mouse_right_clicked(void);  // botão direito foi clicado neste frame
