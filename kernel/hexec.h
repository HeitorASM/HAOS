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
//  ============================================================
//  HEXEC v2 — seções com permissões separadas (W^X real)
//  ============================================================
//
//  A v1 (HEXEC_VERSION 1, ver hexec_header_v1_t abaixo) tratava
//  código+dados como um único bloco RW+USER — sem NX, sem
//  proteção de escrita no código. Isso significa que um bug de
//  ponteiro ou um HEXEC malicioso podia sobrescrever seu próprio
//  código em tempo de execução (nenhuma trava de hardware
//  impedia). A v2 corrige isso separando o executável em até 4
//  seções, cada uma mapeada com as permissões mínimas necessárias:
//
//    .text   — RX  (executável, NUNCA gravável)
//    .rodata — RO  (nem gravável, nem executável)
//    .data   — RW, NX (gravável, nunca executável)
//    .bss    — RW, NX (zero-fill; NÃO ocupa espaço no arquivo)
//
//  Cada seção começa numa página NOVA (alinhada a 4KB) — isso é
//  necessário porque permissões de página no x86_64 são por PTE
//  (4KB), então duas seções com flags diferentes não podem
//  compartilhar a mesma página física sem uma delas "vazar" a
//  permissão da outra. O linker script da libc (libc/hexec.ld)
//  já força esse alinhamento; um HEXEC v2 gerado por outra
//  ferramenta precisa fazer o mesmo.
//
//  Layout do arquivo em disco (v2):
//    [ hexec_header_t (48 bytes, ver abaixo) ]
//    [ text_size bytes, alinhado a 4KB   ]  -> mapeado RX
//    [ rodata_size bytes, alinhado a 4KB ]  -> mapeado RO
//    [ data_size bytes, alinhado a 4KB   ]  -> mapeado RW+NX
//    (bss_size bytes de zero-fill, SEM presença no arquivo,
//     mapeados RW+NX logo após .data)
//
//  Compatibilidade: o campo `version` no início do header é
//  idêntico em v1 e v2 (mesmo offset, mesmo tamanho) — o loader
//  (process.c) lê `version` primeiro e decide qual struct usar
//  para interpretar o resto do header. HEXEC v1 continua
//  funcionando sem qualquer alteração nos executáveis já
//  existentes; não há necessidade de recompilar nada só por causa
//  desta mudança.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define HEXEC_MAGIC       0x43455848U   // "HXEC" em little-endian ("HXEC" -> 0x43455848)
#define HEXEC_VERSION_1   1
#define HEXEC_VERSION_2   2

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

// ---- Header v1 (legado, ainda suportado) ----
// Código+dados+bss como um único bloco RW+USER, sem separação de
// seções. Ver comentário no topo do arquivo — mantido por
// compatibilidade; novos programas devem preferir v2.
typedef struct {
    uint32_t magic;        // deve ser HEXEC_MAGIC
    uint32_t version;      // HEXEC_VERSION_1
    uint64_t entry_offset; // offset (a partir de HEXEC_LOAD_ADDR) do
                            // primeiro RIP em Ring 3
    uint64_t code_size;    // bytes de código+dados presentes NO ARQUIVO
    uint64_t bss_size;     // bytes adicionais de zero-fill após code_size
} __attribute__((packed)) hexec_header_v1_t;

// ---- Header v2 (seções separadas, W^X real) ----
// `version` no mesmo offset/tamanho que hexec_header_v1_t, para que
// o loader possa ler só esse campo antes de decidir qual struct
// usar para o resto — ver parse_hexec_header em process.c.
typedef struct {
    uint32_t magic;         // deve ser HEXEC_MAGIC
    uint32_t version;       // HEXEC_VERSION_2
    uint64_t entry_offset;  // offset (a partir de HEXEC_LOAD_ADDR) do
                             // primeiro RIP em Ring 3 — tipicamente
                             // dentro de .text (offset 0, no início
                             // da seção .text).
    uint64_t text_size;     // bytes de código, alinhados a 4KB no
                             // arquivo — mapeados RX.
    uint64_t rodata_size;   // bytes de dados somente-leitura,
                             // alinhados a 4KB — mapeados RO.
    uint64_t data_size;     // bytes de dados graváveis, alinhados a
                             // 4KB — mapeados RW+NX.
    uint64_t bss_size;      // bytes de zero-fill após .data (NÃO
                             // presentes no arquivo) — mapeados RW+NX.
} __attribute__((packed)) hexec_header_v2_t;

// Maior dos dois headers — usado para reservar espaço suficiente ao
// ler os primeiros bytes de um arquivo sem ainda saber a versão.
#define HEXEC_HEADER_MAX_SIZE  sizeof(hexec_header_v2_t)

#ifdef __cplusplus
}
#endif

