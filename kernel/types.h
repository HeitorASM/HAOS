#pragma once

// ============================================================
//  types.h — Tipos primitivos HAOS (sem stdlib)
//  Compatível com C e C++17 freestanding
// ============================================================

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           uintptr_t;

// size_t DEVE ser o tipo nativo do compilador para que os operadores
// new/delete funcionem. Usamos __SIZE_TYPE__ que o GCC resolve
// automaticamente para 'unsigned long' em x86_64-linux-gnu.
typedef __SIZE_TYPE__      size_t;
typedef __PTRDIFF_TYPE__   ssize_t;

#define NULL  ((void*)0)

// Em C++ usamos bool nativo; em C definimos manualmente
#ifndef __cplusplus
  #define true  1
  #define false 0
  typedef int bool;
#endif

// ---- bool32_t: booleano de tamanho FIXO para structs compartilhadas
// entre C e C++ ----
//
// `bool` (nativo em C++, `typedef int` aqui em C) tem tamanhos
// DIFERENTES nas duas linguagens: 1 byte em C++, 4 bytes neste
// projeto em C. Enquanto esse campo fica isolado entre vizinhos de 8
// bytes (como uint64_t), o padding de alinhamento absorve a
// diferença e o layout da struct acaba batendo por sorte — mas um
// ARRAY de bool, ou vários bool seguidos, NÃO tem essa sorte: o
// mesmo campo cai em offsets diferentes dependendo de quem
// (task.c/.c, ou terminal.cpp/.cpp) está enxergando a struct.
//
// Isso já causou um bug real neste projeto: um `bool
// fd_writable[TASK_MAX_OPEN_FILES]` dentro de task_t fazia
// kernel/task.c (C, bool=4 bytes) e gui/apps/terminal.cpp (C++,
// bool=1 byte) discordarem sobre o offset de TODOS os campos
// seguintes da struct — terminal.cpp escrevia stdout_owner 24 bytes
// adiantado do endereço que syscall.c/task.c liam, corrompendo
// stdin_ring e fazendo a saída do processo simplesmente parar de
// aparecer, sem nenhum erro de compilação.
//
// Regra do projeto: QUALQUER struct definida em um .h incluído tanto
// por código C (kernel/*.c) quanto por código C++ (gui/**/*.cpp) —
// como task_t — usa bool32_t no lugar de bool para qualquer campo
// booleano. bool nativo continua ok para variáveis locais e
// parâmetros de função (que não têm layout binário compartilhado),
// só não em campos de struct que atravessam essa fronteira.
typedef uint32_t bool32_t;
#define BOOL32_TRUE  ((bool32_t)1)
#define BOOL32_FALSE ((bool32_t)0)

// I/O ports — funcionam em C e C++
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void io_wait(void) { outb(0x80, 0); }

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}