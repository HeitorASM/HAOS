#pragma once
#include "../../kernel/types.h"

// ============================================================
//  gui/core/theme_abi.h — a parte do tema que o KERNEL (C) e os programas de
//  usuário enxergam. Só tipos simples e funções extern "C": pode ser incluído
//  por C (kernel/syscall.c) e por C++ (theme.cpp, userwin.cpp).
//
//  Os índices exportados espelham libc/include/haos/uitheme.h — mantenha a ordem.
// ============================================================
#define THEME_ABI_COUNT 33

#ifdef __cplusplus
extern "C" {
#endif

// Mudou algo no tema atual? (hash do conteúdo — evita contar em cada setter)
uint32_t theme_generation(void);

// Preenche out[0..min(cap, THEME_ABI_COUNT)) e devolve THEME_ABI_COUNT.
uint32_t theme_export_abi(uint32_t* out, uint32_t cap);

#ifdef __cplusplus
}
#endif
