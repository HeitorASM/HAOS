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
//
//  Fase 3 (isolamento de espaço de endereço por processo):
//    - Cada processo agora pode ter seu PRÓPRIO PML4 (address_space_t),
//      criado por paging_create_address_space() como uma cópia das
//      entradas de KERNEL (metade superior do PML4, índices 256-511,
//      compartilhadas por todo mundo) mais uma metade inferior
//      (índices 0-255, espaço de usuário) totalmente vazia — pronta
//      para cada processo mapear seu próprio código/dados/pilha sem
//      colidir com o de ninguém.
//    - paging_map()/paging_unmap()/paging_translate() continuam
//      operando sobre o espaço de endereço "atualmente ativo" (o que
//      está de fato carregado em CR3 neste exato momento) — isso é
//      o que o código de kernel (ex.: dentro de uma syscall) espera:
//      "a memória que estou vendo agora". paging_switch_to() é quem
//      muda qual é esse espaço "atual", trocando CR3 de verdade.
//    - As variantes _in() (paging_map_in, etc.) recebem um
//      address_space_t* explícito, para operar sobre um espaço que
//      NÃO é o atualmente ativo — necessário para o loader
//      (process_create) montar a imagem de um processo novo
//      enquanto o CR3 ativo ainda é o do kernel (ou de outro
//      processo), sem precisar trocar de CR3 s\u00f3 para isso.
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

// ---- Regiões de endereço virtual "conhecidas" deste projeto ----
// Centralizadas aqui (em vez de duplicadas em task.c/process.c)
// porque paging_init() precisa garantir, na inicialização, que a
// estrutura de tabelas para a região de pilhas de KERNEL já existe
// antes de qualquer paging_create_address_space() (Fase 3) poder
// ser chamado — ver o comentário extenso em paging_init() sobre o
// bug de Triple Fault que isso evita (a pilha de kernel de um
// processo ficando invisível a partir do próprio CR3 dele).
#define HAOS_KERNEL_STACK_VREGION_BASE   0x200000000ULL   // 8GB — ver task.c

// ---- Inicialização ----
// Constrói uma NOVA hierarquia de tabelas de página (PML4 próprio
// do kernel, com granularidade de 4KB) e substitui o mapeamento
// temporário de 2MB feito em boot.asm. Deve ser chamado depois de
// memory_init() (precisa do PFA já pronto para alocar as tabelas).
void paging_init(void);

// ---- Mapeamento manual (opera no espaço de endereço ATIVO) ----
// Mapeia UMA página de 4KB: endereço virtual -> físico, com as
// flags dadas (combinação de PAGE_*). Aloca tabelas intermediárias
// (PDPT/PD/PT) sob demanda caso ainda não existam. Opera sobre o
// PML4 atualmente carregado em CR3 (ver paging_switch_to) — se
// nenhuma troca de espaço de endereço foi feita, isso é sempre o
// PML4 do kernel, exatamente como antes da Fase 3.
// Retorna false se não houver memória física para as tabelas.
bool paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

// Desmapeia uma página (não libera a física automaticamente —
// isso é responsabilidade de quem gerencia a região, ex.: VMA).
// Opera sobre o espaço de endereço ATIVO — ver paging_map.
void paging_unmap(uint64_t virt_addr);

// Traduz um endereço virtual para físico usando as tabelas do
// espaço de endereço ATIVO. Retorna false se a página não estiver
// mapeada.
bool paging_translate(uint64_t virt_addr, uint64_t* out_phys);

// Retorna true se `virt_addr` está mapeado no espaço de endereço
// ATIVO E com o bit PAGE_USER (acessível a partir de Ring 3). Use
// isto (não só paging_translate) para validar qualquer ponteiro
// vindo de um processo de usuário antes de acessá-lo a partir do
// kernel — uma página mapeada mas SEM PAGE_USER (ex.: a pilha de
// kernel de alguma task, ou o próprio código do kernel) traduziria
// normalmente com paging_translate, mas nunca deveria ser acessível
// via um ponteiro que Ring 3 forneceu.
bool paging_is_user_page(uint64_t virt_addr);

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

// ============================================================
//  Fase 3 — Espaços de endereço isolados por processo
// ============================================================

// Handle opaco para um espaço de endereço (PML4 próprio). O código
// fora de paging.cpp nunca precisa olhar dentro disso — só guarda o
// ponteiro (em task_t, ver task.h) e repassa para as funções desta
// seção.
typedef struct address_space address_space_t;

// Cria um novo espaço de endereço: aloca um PML4 vazio, copia nele
// as 256 entradas SUPERIORES (índices 256-511, endereços virtuais
// >= 0xFFFF800000000000 — mas como este projeto ainda não usa
// higher-half de verdade, na prática isso cobre toda a metade alta
// dos 512 slots de um PML4) do PML4 do KERNEL, e deixa as 256
// entradas inferiores zeradas (espaço de usuário livre para o
// processo mapear o que precisar). Compartilhar as entradas de
// kernel (não as tabelas de nível inferior completas, só os
// ponteiros PDPT de cada entrada de PML4) significa que TODOS os
// espaços de endereço enxergam o mesmo kernel/heap/drivers — só o
// espaço de usuário (metade baixa) é exclusivo de cada processo.
// Retorna NULL em caso de falta de memória.
address_space_t* paging_create_address_space(void);

// Libera um espaço de endereço criado por
// paging_create_address_space(): percorre e libera (devolve ao
// PFA) todas as tabelas PDPT/PD/PT da metade BAIXA (espaço de
// usuário) que pertencem exclusivamente a este espaço, e por fim o
// próprio PML4. NÃO toca nas entradas altas (compartilhadas com o
// kernel) nem nas páginas físicas de DADOS que o processo mapeou
// (código, pilha de usuário) — essas continuam sendo
// responsabilidade de quem as mapeou (process.c/task.c) liberar
// via task_free_mapped_pages ANTES de chamar isto, exatamente como
// já acontece hoje no reaper. Chamar isto sobre o espaço de
// endereço ATUALMENTE ativo (a task que está chamando esta função
// não pode destruir o próprio chão) é um erro do chamador — não há
// checagem aqui, a mesma disciplina do reaper de "nunca colher a
// própria task em execução" se aplica.
void paging_destroy_address_space(address_space_t* as);

// Troca o espaço de endereço ATIVO para `as` (ou para o do kernel,
// se `as` for NULL) — isto é o que de fato escreve em CR3. Chamado
// pelo scheduler (task_switch/do_switch_to) a cada troca de
// contexto, para que paging_map/paging_unmap/paging_translate (e o
// hardware, claro) passem a enxergar o espaço de endereço da task
// que está assumindo a CPU.
void paging_switch_to(address_space_t* as);

// Variantes explícitas de map/unmap/translate que operam sobre um
// address_space_t específico, MESMO QUE ele não seja o ativo no
// momento. Necessário para o loader (process_create, em process.c)
// montar a imagem de um processo novo (código/dados/pilha) sem
// precisar trocar CR3 só para isso — o que seria arriscado de fazer
// a partir de dentro de uma kernel thread que não é o próprio
// processo sendo montado.
//
// `as == NULL` aqui significa "o espaço de endereço ATIVO agora"
// (não "o do kernel" — para isso, use paging_switch_to(NULL) ou
// passe explicitamente o resultado de paging_create_address_space
// que você quer atingir). Essa escolha existe porque
// task_free_mapped_pages() (kernel threads, sem processo de usuário
// nenhum envolvido) sempre quer dizer "onde quer que eu esteja
// rodando agora", e forçar cada chamador a descobrir e passar o
// PML4 do kernel manualmente nesses casos seria só ruído.
bool paging_map_in(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void paging_unmap_in(address_space_t* as, uint64_t virt_addr);
bool paging_translate_in(address_space_t* as, uint64_t virt_addr, uint64_t* out_phys);

// Endereço físico do PML4 de `as` — é isto que vai literalmente
// para CR3. Exposto para task_switch.asm poder ler o CR3 de destino
// a partir de task_t (ver task.h: campo address_space) sem precisar
// entender a estrutura opaca address_space_t.
uint64_t paging_address_space_cr3(address_space_t* as);

#ifdef __cplusplus
}
#endif
