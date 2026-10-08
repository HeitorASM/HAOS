#pragma once
#include "../kernel/types.h"

// Driver xHCI (USB 3.x/2.x) por polling — teclado e mouse HID "boot protocol"
// ligados DIRETO numa porta da placa (hubs ainda não são suportados).
#ifdef __cplusplus
extern "C" {
#endif

int  xhci_init(void);      // devolve quantos dispositivos HID configurou (0 = nenhum/sem xHCI)
void xhci_poll(void);      // chame no loop de entrada (já chamado por keyboard_poll/mouse_process)
bool xhci_present(void);

#ifdef __cplusplus
}
#endif
