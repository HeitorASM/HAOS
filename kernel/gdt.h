#pragma once
#include "types.h"

// ============================================================
//
//  Layout de seletores (ordem escolhida deliberadamente para já
//  ficar compatível com SYSCALL/SYSRET no futuro: o MSR STAR
//  espera Kernel CS, Kernel DS, User CS32(unused), User CS64 em
//  blocos contíguos de 8 bytes a partir de um índice base — por
//  isso User DATA vem ANTES de User CODE aqui, na ordem que o
//  SYSRET de 64-bit espera):
//
//    0x00  null descriptor
//    0x08  kernel code   (64-bit, DPL0)
//    0x10  kernel data   (DPL0)
//    0x18  user data     (DPL3)
//    0x20  user code     (64-bit, DPL3)
//    0x28  TSS           (descriptor de 16 bytes — ocupa 0x28 e 0x30)
// ============================================================

#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_DATA    (0x18 | 3)   // RPL=3 já embutido no seletor
#define GDT_USER_CODE    (0x20 | 3)   // RPL=3 já embutido no seletor
#define GDT_TSS          0x28

#ifdef __cplusplus
extern "C" {
#endif

void gdt_init(void);

// Atualiza o campo RSP0 da TSS — o stack que a CPU vai usar
// automaticamente sempre que uma interrupção/exceção ocorrer
// enquanto o processador está em Ring 3. TEM que ser chamado
// toda vez que trocamos de contexto/processo (cada processo
// precisa da sua própria stack de kernel para entrar em Ring 0
// com segurança). Por enquanto, sem scheduler, é chamado uma
// única vez em gdt_init() com a stack de boot do kernel.
void tss_set_kernel_stack(uint64_t rsp0);

#ifdef __cplusplus
}
#endif
