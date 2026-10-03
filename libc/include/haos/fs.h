#pragma once
#include "types.h"

// ============================================================
//  libc/include/haos/fs.h — estruturas de SYS_STAT / SYS_READDIR
//
//  ESPELHA haos_stat_t / haos_dirent_t de kernel/syscall.h — o layout
//  precisa ser idêntico byte a byte (mesma regra de haos/syscall.h:
//  a libc não inclui headers de kernel/, então duplicamos).
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define HAOS_TYPE_DIR   0
#define HAOS_TYPE_FILE  1
#define HAOS_NAME_MAX   64   // inclui o '\0'

struct haos_stat {
    uint32_t type;   // HAOS_TYPE_DIR ou HAOS_TYPE_FILE
    uint32_t size;   // arquivo: bytes; diretório: nº de entradas
};

struct haos_dirent {
    uint32_t type;
    uint32_t size;                 // idem haos_stat::size
    char     name[HAOS_NAME_MAX];  // terminado em '\0'
};

// whence de lseek()
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifdef __cplusplus
}
#endif
