#pragma once
#include "haos/types.h"

// ============================================================
//  string.h — funções básicas de memória e string (freestanding)
//
//  Implementações simples e diretas, sem otimizações de SIMD (o
//  ambiente é -mno-sse/-mno-mmx, então nem poderiam usar essas
//  instruções) — priorizam corretude e clareza sobre performance.
//  Suficiente para o uso típico de um programa pequeno; revisar se
//  algum dia um HEXEC precisar copiar blocos muito grandes de dados
//  em loop quente.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

void*  memcpy(void* dst, const void* src, size_t n);
void*  memset(void* dst, int value, size_t n);
void*  memmove(void* dst, const void* src, size_t n);
int    memcmp(const void* a, const void* b, size_t n);

size_t strlen(const char* s);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, size_t n);
char*  strcat(char* dst, const char* src);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);

#ifdef __cplusplus
}
#endif
