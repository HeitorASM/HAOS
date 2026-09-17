#pragma once
#include "types.h"

// ============================================================
//  kernel/hexec.h — Formato de executável do HAOS ("HEXEC")
//
//  Por quê não ELF? ELF exige parsing de program headers,
//  section headers, possivelmente relocations e symbol tables —
//  trabalho substancial que não agrega nada nesta fase (provar o
//  pipeline completo: loader -> processo de usuário -> syscall ->
//  scheduler). HEXEC é um formato flat deliberadamente mínimo:
//  um header fixo seguido de código+dados prontos para copiar
//  direto na memória, sem relocação (o executável é compilado/
//  linkado para rodar sempre no mesmo endereço virtual fixo,
//  HEXEC_LOAD_ADDR). Migrar para ELF64 fica como evolução futura
//  natural quando processos precisarem de bibliotecas dinâmicas
//  ou não for mais aceitável fixar o endereço de carga.
//
//  Layout do arquivo em disco:
//    [ hexec_header_t (32 bytes) ]
//    [ code_size bytes de código+dados, carregados sequencialmente
//      a partir de HEXEC_LOAD_ADDR ]
//
//  bss_size bytes ADICIONAIS (zero-fill, não presentes no arquivo)
//  são mapeados logo após o código+dados — isso permite variáveis
//  globais não inicializadas sem inflar o arquivo em disco.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define HEXEC_MAGIC       0x43455848U   // "HXEC" em little-endian ("HXEC" -> 0x43455848)
#define HEXEC_VERSION     1

// Endereço virtual fixo onde TODO executável HEXEC é carregado —
// mas isso já não implica mais colisão entre processos diferentes.
// Desde a Fase 3 (isolamento de espaço de endereço, ver paging.h:
// address_space_t), cada processo tem seu PRÓPRIO PML4 com uma
// metade de usuário exclusiva. "HEXEC_LOAD_ADDR" é uma coordenada
// dentro do espaço de endereço de CADA processo — dois processos
// diferentes usando o mesmo valor aqui são, na prática, dois
// endereços físicos completamente distintos, um em cada tabela de
// páginas. Isso significa que múltiplos processos HEXEC podem
// coexistir e rodar ao mesmo tempo sem qualquer conflito — a
// limitação de "um processo por vez" que existia nas Fases 1-2 foi
// removida junto com a introdução de address_space_t.
//
// Mantemos um endereço fixo (em vez de escolher um livre
// dinamicamente por processo) porque simplifica o loader: sem
// relocação nem PIC (Position Independent Code) no formato HEXEC,
// todo executável já é compilado/montado assumindo este endereço
// como base fixa.
#define HEXEC_LOAD_ADDR   0x300000000ULL   // 12GB virtual (dentro do
                                             // espaço de endereço de
                                             // CADA processo)

typedef struct {
    uint32_t magic;        // deve ser HEXEC_MAGIC
    uint32_t version;      // HEXEC_VERSION
    uint64_t entry_offset; // offset (a partir de HEXEC_LOAD_ADDR) do
                            // primeiro RIP em Ring 3 — não precisa ser 0
                            // se o executável tiver algum preâmbulo antes
                            // do "main" de fato.
    uint64_t code_size;    // bytes de código+dados presentes NO ARQUIVO,
                            // copiados literalmente para a memória.
    uint64_t bss_size;     // bytes adicionais de zero-fill após code_size
                            // (variáveis globais não inicializadas).
} __attribute__((packed)) hexec_header_t;

#ifdef __cplusplus
}
#endif
