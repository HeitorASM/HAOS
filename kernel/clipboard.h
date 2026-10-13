#pragma once
#include "types.h"

// ============================================================
//  kernel/clipboard.h — área de transferência de TEXTO do sistema
//
//  Compartilhada pelo Editor e pelos programas de usuário (SYS_CLIPBOARD).
//  Só há uma sessão de desktop por vez, então não há separação por usuário.
//  (A área de transferência de ARQUIVOS do Explorador é outra, por caminho.)
// ============================================================
#define CLIPBOARD_SYS_MAX 65536   // bytes de texto, sem o NUL

#ifdef __cplusplus
extern "C" {
#endif

// Substitui o conteúdo (len > CLIPBOARD_SYS_MAX é truncado). len == 0 limpa.
void     clipboard_set(const char* data, uint32_t len);

// Copia até `cap` bytes para `out` e devolve o TAMANHO TOTAL guardado (pode
// ser maior que `cap`; nesse caso `out` recebe só o começo). Não escreve NUL.
uint32_t clipboard_get(char* out, uint32_t cap);

uint32_t clipboard_len(void);

#ifdef __cplusplus
}
#endif
