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

// ---- Alocação dinâmica de memória ----
//
// Implementação: first-fit sobre uma lista encadeada de blocos
// livres, com sbrk() (ver unistd.h) para obter mais memória do
// kernel quando a lista não tem nenhum bloco livre grande o
// suficiente. Blocos liberados por free() são devolvidos à lista
// (e blocos livres ADJACENTES na memória são fundidos em um só,
// para reduzir fragmentação) — mas a memória NUNCA é devolvida ao
// kernel via sbrk negativo; uma vez que o processo cresceu sua
// heap, ela só encolhe quando o processo termina (o kernel libera
// tudo de uma vez no reaper). Isso é uma simplificação deliberada:
// decidir COM SEGURANÇA quando é seguro encolher a heap de volta
// exigiria rastrear se o topo dela está realmente livre até onde
// sbrk(0) aponta, o que esta implementação simples não faz.
//
// Não é thread-safe (não há necessidade nesta fase: cada processo
// HAOS é single-threaded — não existe a possibilidade de duas
// "threads" do mesmo processo chamando malloc() ao mesmo tempo).
void* malloc(size_t size);
void  free(void* ptr);

// realloc: cresce ou encolhe um bloco já alocado, preservando o
// conteúdo (até o menor dos dois tamanhos). Implementação simples:
// sempre aloca um bloco novo e copia — não tenta expandir um bloco
// existente no lugar mesmo quando isso seria possível. Otimizar isso
// é uma melhoria futura razoável se perfilamento mostrar que vale a
// pena.
void* realloc(void* ptr, size_t new_size);

#ifdef __cplusplus
}
#endif
