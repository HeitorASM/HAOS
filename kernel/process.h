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

#define PROCESS_USER_STACK_PAGES   16  // 64KB de pilha de USUÁRIO —
                                        // aumentado de 4 (16KB) depois
                                        // de um travamento real
                                        // causado por overflow
                                        // silencioso: sem guard page
                                        // (ver USER_STACK_GAP_PAGES em
                                        // process.c, que agora É uma
                                        // proteção real), uma pilha de
                                        // 16KB estourava com poucas
                                        // chamadas de printf/malloc
                                        // aninhadas, corrompendo os
                                        // primeiros blocos da heap
                                        // (que ficava logo depois) de
                                        // forma silenciosa — sem
                                        // nenhum #PF, porque a página
                                        // "depois" da pilha era
                                        // simplesmente heap ainda não
                                        // mapeada, não uma região
                                        // protegida contra escrita.
                                        // 64KB dá bastante margem para
                                        // um programa simples; ver
                                        // também a guard page real
                                        // adicionada entre pilha e
                                        // heap, que agora transforma
                                        // um estouro remanescente num
                                        // #PF diagnosticável em vez de
                                        // corrupção silenciosa.

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
// `argv_str`: argumentos de linha de comando, concatenados como
// strings C consecutivas terminadas em '\0' cada uma, com uma string
// vazia extra ao final marcando o fim da lista (formato: "arg1\0arg2\0\0")
// — o próprio argv[0] (nome do programa) deve ser o primeiro
// "argumento" dessa sequência, seguindo a convenção Unix. Passe NULL
// (ou uma string vazia seguida de outra vazia, "\0\0") para nenhum
// argumento — nesse caso argc=0 e argv[0]=NULL no crt0 do programa.
// `argv_len`: tamanho TOTAL de argv_str em bytes, incluindo todos os
// terminadores '\0' (necessário porque a string tem embutidos vários
// '\0' internos — strlen() sozinho pegaria só o primeiro argumento).
//
// Múltiplos processos podem existir simultaneamente (Fase 3) — cada
// chamada cria um espaço de endereço independente.
//
// Retorna a task criada (já TASK_READY) ou NULL se falhar (arquivo
// não é um HEXEC válido, tabela de tasks cheia, ou sem memória). Em
// caso de NULL, nenhuma página fica vazando.
struct VfsNode;
task_t* process_create(const char* name, struct VfsNode* executable_file,
                        const char* argv_str, uint64_t argv_len);

// ---- API interna reutilizada por syscall.c (sys_sbrk) ----
//
// Estas duas funções eram `static` só de uso interno de
// process_create() até a syscall SYS_SBRK precisar delas para
// crescer a heap de usuário sob demanda — expor a MESMA
// implementação evita duplicar a lógica (nada sutil) de "mapear N
// páginas num address_space que pode não estar ativo, com rollback
// completo em caso de falha parcial".

// Mapeia `pages` páginas físicas novas a partir de `virt_start`,
// dentro do espaço de endereço `as`. Ver implementação em process.c
// para o comportamento completo de cópia de dados/zero-fill e
// rollback em falha parcial.
bool map_region_in(address_space_t* as, uint64_t virt_start,
                    uint64_t pages, uint64_t flags,
                    const uint8_t* src_data, uint64_t src_len);

// Arredonda `bytes` para cima, em unidades de PAGE_SIZE_4K.
uint64_t pages_needed_for(uint64_t bytes);

#ifdef __cplusplus
}
#endif
