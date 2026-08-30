#pragma once
#include "types.h"


//  IMPORTANTE: esta etapa entrega só a INFRAESTRUTURA. Ainda não
//  existe um programa de usuário real para carregar (isso vem
//  junto com o carregador de executáveis + syscalls, próxima
//  etapa). enter_usermode() está pronta e testável isoladamente,
//  mas só faz sentido chamá-la quando "entry_point" apontar para
//  código de fato mapeado com PAGE_USER (ver paging.h).

#ifdef __cplusplus
extern "C" {
#endif

// Executa a transição para Ring 3 e NUNCA RETORNA para quem chamou
// (o "retorno" natural passa a ser via syscall de exit, quando essa
// existir). Monta um frame de iretq com os seletores de usuário
// (GDT_USER_CODE/GDT_USER_DATA) e salta para entry_point, usando
// user_stack_top como RSP inicial em Ring 3.
//
// Pré-requisitos que quem chamar PRECISA garantir:
//   1. paging_init() já foi chamado.
//   2. A página contendo entry_point está mapeada com PAGE_USER
//      (senão a CPU gera #GP/#PF assim que tentar buscar a
//      instrução em Ring 3 — o handler de exceção da etapa 1 vai
//      capturar isso com um panic detalhado, não trava silenciosamente).
//   3. user_stack_top aponta para o TOPO de uma região também
//      mapeada com PAGE_USER (a pilha cresce para baixo).
//   4. tss_set_kernel_stack() foi chamado com uma stack de kernel
//      válida para este contexto (senão a próxima interrupção que
//      ocorrer em Ring 3 usa um RSP0 desatualizado/inválido).

#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void enter_usermode(uint64_t entry_point, uint64_t user_stack_top);

#ifdef __cplusplus
}
#endif
