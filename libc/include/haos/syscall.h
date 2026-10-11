#pragma once
#include "types.h"

// ============================================================
//  libc/include/haos/syscall.h — Interface crua de syscall
//
//  Espelha exatamente kernel/syscall.h (números de syscall e
//  convenção de registradores) — se um dia divergirem, é sinal de
//  que uma nova syscall foi adicionada no kernel sem o espelho
//  correspondente ser atualizado aqui. Mantidos como arquivos
//  separados de propósito (não um #include compartilhado): a libc
//  não deve depender de headers de kernel/, e a duplicação de um
//  arquivo pequeno como este é um preço aceitável por essa fronteira
//  clara. Ao adicionar uma syscall nova no kernel, adicione o par
//  correspondente aqui também.
//
//  Convenção (System V AMD64, adaptada para a instrução SYSCALL):
//    rax = número da syscall
//    rdi, rsi, rdx, r10, r8, r9 = argumentos (nessa ordem — usa r10
//      no lugar de rcx porque SYSCALL destrói rcx/r11 automaticamente)
//    retorno em rax
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_SBRK   2
#define SYS_OPEN   3
#define SYS_READ   4
#define SYS_CLOSE  5
#define SYS_WRITE_FD 6
#define SYS_SPAWN 7
#define SYS_WAITPID 8
#define SYS_GETPID    9
#define SYS_YIELD     10
#define SYS_UPTIME_MS 11
#define SYS_SLEEP_MS  12
#define SYS_STAT      13
#define SYS_READDIR   14
#define SYS_GETCWD    15
#define SYS_CHDIR     16
#define SYS_MKDIR     17
#define SYS_UNLINK    18
#define SYS_LSEEK     19
#define SYS_WIN_CREATE  20
#define SYS_WIN_DESTROY 21
#define SYS_WIN_FILL    22
#define SYS_WIN_TEXT    23
#define SYS_WIN_BLIT    24
#define SYS_WIN_EVENT   25
#define SYS_WIN_INFO    26
#define SYS_GETUID      27
#define SYS_GETGID      28
#define SYS_CHMOD       29
#define SYS_GETRANDOM   30
#define SYS_RENAME      31
#define SYS_SET_WALLPAPER 32

// Flags de open2() — espelham os O_* de kernel/syscall.h.
#ifndef O_RDONLY
#define O_RDONLY 0x0
#define O_WRONLY 0x1
#define O_CREAT  0x2
#define O_TRUNC  0x4
#endif

// Syscall crua de até 5 argumentos — usada internamente pelos
// wrappers de mais alto nível (write(), _exit(), ver unistd.h).
// Programas de usuário normalmente não chamam isto diretamente;
// preferem as funções POSIX-like que fazem a checagem de tipo
// correta para cada syscall específica.
long haos_syscall(long num, long a1, long a2, long a3, long a4, long a5);

#ifdef __cplusplus
}
#endif
