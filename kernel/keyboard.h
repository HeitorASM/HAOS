#pragma once
#include "types.h"

// ---- Códigos especiais devolvidos por keyboard_getchar() ----
// (valores > 0x7F não colidem com ASCII imprimível/controle normal)
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_F2    0x84   // usado pelo editor de texto para "Salvar"

#define KEY_CTRL_S 0x85  // Ctrl+S -- salvar
#define KEY_CTRL_C 0x86  // Ctrl+C -- copiar
#define KEY_CTRL_V 0x87  // Ctrl+V -- colar
#define KEY_CTRL_X 0x88  // Ctrl+X -- recortar
#define KEY_CTRL_A 0x89  // Ctrl+A -- selecionar tudo

#define KEY_SHIFT_UP    0x8A
#define KEY_SHIFT_DOWN  0x8B
#define KEY_SHIFT_LEFT  0x8C
#define KEY_SHIFT_RIGHT 0x8D

// ASCII de controle padrão já usados pelo teclado (não redefinidos
// aqui, só documentados para quem for tratar KeyDown num widget):
//   0x08 = Backspace, 0x0A = Enter/Newline, 0x1B = Esc

#ifdef __cplusplus
extern "C" {
#endif

void     keyboard_init(void);
void     keyboard_poll(void);
uint8_t  keyboard_getchar(void);
bool     keyboard_has_data(void);

#ifdef __cplusplus
}
#endif
