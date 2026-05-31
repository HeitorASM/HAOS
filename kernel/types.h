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