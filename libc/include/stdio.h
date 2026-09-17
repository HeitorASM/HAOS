#pragma once
#include "haos/types.h"

// ============================================================
//  stdio.h — E/S formatada (STUB por enquanto)
//
//  Deliberadamente vazio de funções nesta primeira versão da libc.
//  printf() exige um formatador de string (parsing de %d/%s/%x/...)
//  que é trabalho não-trivial por si só, e depende de write() já
//  estar sólido — por isso primeiro entregamos write()/_exit() (ver
//  unistd.h) funcionando de ponta a ponta, e puts()/printf() entram
//  numa iteração seguinte da libc, quando fizer sentido investir
//  nisso. Até lá, use write() diretamente para qualquer saída de
//  texto.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// (nenhuma função ainda — ver comentário acima)

#ifdef __cplusplus
}
#endif
