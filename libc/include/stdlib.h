#pragma once
#include "haos/types.h"

// ============================================================
//  stdlib.h — utilidades gerais de programa
//
//  Hoje só tem exit(). malloc()/free() ficam para quando o kernel
//  ganhar uma syscall de gerência de memória de usuário (sbrk/mmap)
//  — até lá, um programa HEXEC só tem a memória que o loader já
//  mapeou para ele (código+dados+bss+pilha), sem crescimento
//  dinâmico possível.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Encerra o processo com o código dado. Nesta versão da libc é
// idêntica a _exit() (ver unistd.h) — a distinção existe desde já
// na API para o dia em que o HAOS ganhar um conceito de handlers de
// saída (atexit()) ou buffers de stdio a serem descarregados antes
// de sair; programas escritos contra exit() já vão se comportar
// certo quando isso existir, sem precisar ser reescritos.
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void exit(int status);

#ifdef __cplusplus
}
#endif
