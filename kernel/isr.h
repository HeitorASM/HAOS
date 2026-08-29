#pragma once
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Estado completo da CPU no momento da exceção.
// A ORDEM DOS CAMPOS TEM QUE BATER EXATAMENTE com a ordem de
// push/pop em kernel/isr_asm.asm — não reordene sem atualizar os dois.
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;       // número da exceção (0-31)
    uint64_t error_code;   // código de erro da CPU (0 se não aplicável)
    // Empilhados automaticamente pela CPU no momento da interrupção:
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) regs_t;

// Handler de exceção customizável (usado por outros subsistemas,
// por exemplo o gestor de paginação vai registrar seu próprio
// tratador para o vetor 14 / #PF quando formos implementá-lo).
typedef void (*isr_handler_fn)(regs_t* regs);

// Instala os 32 stubs de exceção na IDT. Deve ser chamado de
// dentro de idt_init(), depois de idt_set() para os vetores 0-31
// já estar disponível.
void isr_install(void);

// Permite que outro subsistema (ex.: paginação) assuma o
// tratamento de um vetor específico (ex.: 14 = Page Fault),
// em vez do handler padrão (que sempre entra em panic).
void isr_register_handler(uint8_t vector, isr_handler_fn fn);

// Chamado pelo stub em assembly.
void isr_handler(regs_t* regs);

#ifdef __cplusplus
}
#endif
