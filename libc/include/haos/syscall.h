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

// Syscall crua de até 5 argumentos — usada internamente pelos
// wrappers de mais alto nível (write(), _exit(), ver unistd.h).
// Programas de usuário normalmente não chamam isto diretamente;
// preferem as funções POSIX-like que fazem a checagem de tipo
// correta para cada syscall específica.
long haos_syscall(long num, long a1, long a2, long a3, long a4, long a5);

#ifdef __cplusplus
}
#endif
