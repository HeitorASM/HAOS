#pragma once
#include "types.h"
#include "task.h"

// ============================================================
//  kernel/process.h — Processos de usuário reais (Fase 2 + 3)
//
//  Constrói sobre a infraestrutura da Fase 1 (task_t, scheduler,
//  guard pages) e sobre a infraestrutura de Ring 3 que já existia
//  antes do scheduler (enter_usermode, syscall/sysret, TSS). Um
//  "processo" aqui é uma task_t com is_user_task=true: mesma
//  estrutura de agendamento, mas a primeira execução transita para
//  Ring 3 em vez de chamar uma função de kernel diretamente.
//
//  FASE 3 (ISOLAMENTO DE ESPAÇO DE ENDEREÇO): cada processo agora
//  recebe seu próprio espaço de endereço (address_space_t, ver
//  paging.h), criado por paging_create_address_space() — um PML4
//  cuja metade de usuário é exclusiva do processo e cuja metade de
//  kernel é compartilhada com todos (mesmas tabelas do kernel). Isso
//  significa que múltiplos processos podem coexistir e rodar ao
//  mesmo tempo sem colidir, mesmo carregando no mesmo endereço
//  virtual fixo (HEXEC_LOAD_ADDR) — cada um enxerga sua própria
//  tradução para esse endereço. A limitação de "só um processo por
//  vez" que existia antes desta fase não se aplica mais.
//
//  Dívida técnica ainda existente: código e dados de um processo
//  continuam na mesma região RW (sem separar .text somente-leitura/
//  executável de dados graváveis) — um HEXEC malicioso ainda pode
//  sobrescrever seu próprio código. Resolver isso exige o formato
//  HEXEC descrever tamanhos de seção individualmente (mais próximo
//  de ELF), o que fica para quando migrarmos de formato.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define PROCESS_USER_STACK_PAGES   4   // 16KB de pilha de USUÁRIO —
                                        // separada da pilha de KERNEL
                                        // que toda task já tem (essa
                                        // atende ao RSP0 da TSS para
                                        // tratar interrupções/syscalls
                                        // vindas de Ring 3).

// Carrega um executável HEXEC a partir de um VfsNode (arquivo já
// lido em memória via VFS — ver fs/vfs.h) e cria um processo de
// usuário para rodá-lo: cria um espaço de endereço isolado próprio,
// mapeia código+dados+bss em HEXEC_LOAD_ADDR (dentro desse espaço)
// com PAGE_USER, aloca uma pilha de usuário própria (também isolada),
// aloca a pilha de kernel padrão de toda task (compartilhada, com
// guard page), e insere na fila do scheduler. O processo começa a
// rodar na próxima vez que o scheduler escaloná-lo — não há execução
// síncrona aqui.
//
// Múltiplos processos podem existir simultaneamente (Fase 3) — cada
// chamada cria um espaço de endereço independente.
//
// Retorna a task criada (já TASK_READY) ou NULL se falhar (arquivo
// não é um HEXEC válido, tabela de tasks cheia, ou sem memória). Em
// caso de NULL, nenhuma página fica vazando.
struct VfsNode;
task_t* process_create(const char* name, struct VfsNode* executable_file);

#ifdef __cplusplus
}
#endif
