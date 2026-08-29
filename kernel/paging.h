#pragma once
#include "types.h"

// ============================================================
//  Paginação do HAOS (substitui o mapeamento
//  de 2MB feito em boot.asm)
//
//  Contexto:
//    O boot.asm mapeia identity 0..4GB inteiro com páginas de
//    2MB, tudo RW+Present, sem NX, sem distinção de privilégio.
//    Isso foi necessário só para o salto pra long mode funcionar
//    (a CPU exige paginação ativa para entrar em 64-bit). A partir
//    daqui o kernel assume o controle real das tabelas de página,
//    com granularidade de 4KB e permissões por página.
//
//  Nesta primeira etapa:
//    - Continuamos com identity-map do kernel (sem higher-half
//      ainda — isso fica para quando implementarmos Ring 3 com
//      espaços de endereço por processo).
//    - Introduzimos permissões reais por página: RW, NX (No-Execute),
//      User/Supervisor.
//    - Introduzimos "demand paging": regiões podem ser reservadas
//      (VMA) sem página física associada; a primeira escrita/leitura
//      causa #PF, que o handler resolve alocando e mapeando sob
//      demanda, em vez de exigir tudo pré-alocado.
//    - O #PF deixa de ser sempre fatal: só é panic se o endereço
//      não pertence a nenhuma região válida conhecida.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ---- Flags de permissão de página (independentes de arquitetura) ----
#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_WRITABLE  (1ULL << 1)
#define PAGE_USER      (1ULL << 2)   // acessível a partir de Ring 3
#define PAGE_NO_EXEC   (1ULL << 63)  // bit NX (requer EFER.NXE, ativado em paging_init)

// Tamanho de página padrão usado por todo o subsistema (4KB).
#define PAGE_SIZE_4K   4096ULL

// ---- Inicialização ----
// Constrói uma NOVA hierarquia de tabelas de página (PML4 próprio
// do kernel, com granularidade de 4KB) e substitui o mapeamento
// temporário de 2MB feito em boot.asm. Deve ser chamado depois de
// memory_init() (precisa do PFA já pronto para alocar as tabelas).
void paging_init(void);

// ---- Mapeamento manual ----
// Mapeia UMA página de 4KB: endereço virtual -> físico, com as
// flags dadas (combinação de PAGE_*). Aloca tabelas intermediárias
// (PDPT/PD/PT) sob demanda caso ainda não existam.
// Retorna false se não houver memória física para as tabelas.
bool paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

// Desmapeia uma página (não libera a física automaticamente —
// isso é responsabilidade de quem gerencia a região, ex.: VMA).
void paging_unmap(uint64_t virt_addr);

// Traduz um endereço virtual para físico usando as tabelas atuais.
// Retorna false se a página não estiver mapeada.
bool paging_translate(uint64_t virt_addr, uint64_t* out_phys);

// ---- Regiões com mapeamento sob demanda (demand paging) ----
// Reserva uma faixa [virt_addr, virt_addr+size) como "válida mas
// não mapeada". Um #PF dentro dessa faixa aloca e mapeia a página
// automaticamente (zero-fill) em vez de causar panic. Use para a
// heap crescer sem precisar mapear tudo de uma vez, ou futuramente
// para pilhas de processos.
bool paging_reserve_region(uint64_t virt_addr, uint64_t size, uint64_t flags);

// ---- Estatísticas ----
uint64_t paging_get_mapped_pages(void);
uint64_t paging_get_demand_faults(void);   // quantos #PF foram resolvidos por demand paging

#ifdef __cplusplus
}
#endif
