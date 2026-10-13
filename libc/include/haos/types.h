#pragma once
// ============================================================
//  libc/include/haos/types.h — Tipos primitivos da libc do HAOS
//
//  Deliberadamente separado de kernel/types.h: a libc é código que
//  roda em RING 3, dentro de um processo de usuário — ela não deve
//  depender de nada que viva em kernel/, mesmo que hoje ambos sejam
//  compilados no mesmo passo de build. Essa separação existe para
//  deixar claro, desde já, onde fica a fronteira entre "código de
//  kernel" e "código de aplicação", mesmo que os dois arquivos
//  pareçam duplicados por enquanto.
// ============================================================

// Testes de HOST (tests/libui, tests/ui) compilam a libc com -DHAOS_HOST_BUILD e usam os
// tipos do compilador/glibc; os typedefs abaixo só valem no HAOS (conflitam com <stdint.h>).
#ifdef HAOS_HOST_BUILD
#  include <stdint.h>
#  include <stddef.h>
#  include <sys/types.h>
#else
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           uintptr_t;

typedef __SIZE_TYPE__      size_t;
typedef __PTRDIFF_TYPE__   ssize_t;

#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef __cplusplus
  #define true  1
  #define false 0
  typedef int bool;
#endif
