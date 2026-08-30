#pragma once
#include "types.h"

// ============================================================
//  syscall.h — Syscalls do HAOS via SYSCALL/SYSRET
//
//  Convenção de chamada (System V AMD64, adaptada para syscall):
//    rax = número da syscall
//    rdi, rsi, rdx, r10, r8, r9 = argumentos (nessa ordem)
//      (usa r10 no lugar de rcx porque SYSCALL destrói rcx/r11
//       automaticamente — a ABI padrão do Linux x86_64 faz o mesmo
//       ajuste pelo mesmo motivo)
//    retorno em rax
//
//  Um processo de usuário simplesmente executa a instrução
//  `syscall` — não existe int 0x80 aqui, usamos o mecanismo mais
//  moderno e mais rápido (SYSCALL/SYSRET, sem passar pela IDT).
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ---- Números de syscall (tabela mínima para o teste desta etapa) ----
#define SYS_WRITE  0   // sys_write(const char* str, uint64_t len) -> bytes escritos
#define SYS_EXIT   1   // sys_exit(int code) -> nunca retorna
#define SYS_COUNT  2

// Configura os MSRs necessários (STAR, LSTAR, SFMASK) e habilita
// EFER.SCE (System Call Extensions). Deve ser chamado depois de
// gdt_init() (precisa dos seletores de kernel/usuário já definidos)
// e antes de qualquer enter_usermode().
void syscall_init(void);

// Dispatcher chamado pelo stub em assembly (syscall_asm.asm). Não
// chame diretamente — é o ponto de entrada em C de toda syscall.
// Suporta até 5 argumentos (a1..a5), suficiente para sys_write
// (ptr, len) e sys_exit (code); pode ser estendido para 6 se algum
// dia for necessário (ver comentário em syscall_asm.asm).
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);

#ifdef __cplusplus
}
#endif
