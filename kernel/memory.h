#pragma once
#include "types.h"

// ============================================================
//  memory.h — API pública do subsistema de memória HAOS
//
//  Hierarquia:
//    1. PageFrameAllocator (PFA) — aloca/liberta páginas físicas de 4KB
//       via bitmap, usando o mapa de memória do Multiboot2.
//    2. KernelHeap — alocador de heap por free-list encadeada,
//       obtendo páginas do PFA.
//    3. Operadores new/delete (em memory.cpp) — chamam a KernelHeap.
//    4. kmalloc/kfree — wrappers em C para o restante código C.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ---- Inicialização principal ----
// Recebe o ponteiro para o boot info do Multiboot2.
// Deve ser chamado antes de qualquer alocação.
void memory_init(uint32_t mb_info_raw);

// ---- API C de baixo nível (usada pelo código C existente) ----
void*  kmalloc(size_t n);
void*  kzalloc(size_t n);
void   kfree(void* p);

// ---- Utilitários de string/mem (sem libc) ----
void*  kmemset(void* dst, int val, size_t n);
void*  kmemcpy(void* dst, const void* src, size_t n);
int    kstrcmp(const char* a, const char* b);
int    kstrncmp(const char* a, const char* b, size_t n);
size_t kstrlen(const char* s);
char*  kstrcpy(char* dst, const char* src);
char*  kstrncpy(char* dst, const char* src, size_t n);
char*  kstrcat(char* dst, const char* src);
void   kitoa(int64_t n, char* buf);
void   kuitoa(uint64_t n, char* buf);
void   kuitoa_hex(uint64_t n, char* buf);

// ---- Estatísticas de memória (para HardwareInfo) ----
uint64_t mem_get_total_bytes(void);   // RAM total detectada
uint64_t mem_get_free_pages(void);    // Páginas físicas livres
uint64_t mem_get_used_pages(void);    // Páginas físicas usadas
uint64_t mem_get_heap_used(void);     // Bytes usados na heap do kernel

#ifdef __cplusplus
} // extern "C"
#endif
