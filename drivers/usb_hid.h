#pragma once
#include "../kernel/types.h"

// Tradução de relatórios HID "boot protocol" para o resto do HAOS:
//   teclado -> scancodes PS/2 (set 1), que keyboard.c já sabe interpretar
//   mouse   -> mouse_inject()
#ifdef __cplusplus
extern "C" {
#endif

void usb_hid_keyboard_report(const uint8_t* report, uint32_t len);   // 8 bytes
void usb_hid_mouse_report(const uint8_t* report, uint32_t len);      // 3+ bytes
void usb_hid_keyboard_reset(void);   // teclado desconectado: solta todas as teclas
void usb_hid_tick(void);             // repetição de tecla (teclado USB não repete sozinho)

#ifdef __cplusplus
}
#endif
