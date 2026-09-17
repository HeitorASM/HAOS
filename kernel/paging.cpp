// ============================================================
//  Paginação HAOS
//
//  Substitui o mapeamento temporário de 2MB feito em boot.asm por
//  uma hierarquia de tabelas de página com granularidade de 4KB,
//  gerenciada dinamicamente pelo kernel.
//
//  Ainda usamos identity-map (virt == phys) para o range 0..4GB,
//  igual ao boot.asm — a diferença crucial não é ONDE as coisas
//  são mapeadas, é COMO: agora cada página de 4KB tem suas próprias
//  flags (RW, NX, User), pode ser desmapeada individualmente, e
//  passamos a ter "demand paging" real através do handler de #PF.
//
//  Nomenclatura x86_64 padrão (4 níveis, paginação de 4KB):
//    PML4 -> PDPT -> PD -> PT -> página física de 4KB
//  Cada tabela tem 512 entradas de 8 bytes (4096 bytes no total).
// ============================================================

#include "paging.h"
#include "memory.h"
#include "isr.h"
#include "hexec.h" // HEXEC_LOAD_ADDR — usado para calcular
                    // dinamicamente qual entrada de PDPT deve ficar
                    // exclusiva de cada processo (ver
                    // paging_create_address_space).
#include "types.h"
#include "../drivers/fb.h"

namespace {

// ---- Entrada de tabela de página (formato x86_64 padrão) ----
using pte_t = uint64_t;

static constexpr uint64_t ADDR_MASK   = 0x000FFFFFFFFFF000ULL; // bits 12-51
static constexpr int      ENTRIES     = 512;

// s_kernel_pml4: o PML4 original do kernel, criado em paging_init().
// Nunca é destruído, e é a "fonte da verdade" para as entradas de
// kernel (índices 256-511) que todo processo precisa enxergar.
//
// s_active_pml4: o PML4 CORRENTEMENTE carregado em CR3. Antes da
// Fase 3, estes dois eram sempre a mesma coisa (só existia um PML4
// no sistema inteiro). Agora, paging_switch_to() pode apontar
// s_active_pml4 para o PML4 de um processo — e paging_map/unmap/
// translate (sem sufixo _in) sempre operam sobre s_active_pml4, não
// sobre s_kernel_pml4, para que código de kernel rodando "dentro"
// de uma syscall atendendo um processo enxergue a memória DAQUELE
// processo, não sempre a do kernel.
static pte_t* s_kernel_pml4 = nullptr;
static pte_t* s_active_pml4 = nullptr;

static uint64_t s_mapped_pages   = 0;
static uint64_t s_demand_faults  = 0;

// ---- Região reservada para demand paging (bem simples: uma única
//      faixa por enquanto — suficiente para a heap crescer sob
//      demanda; quando tivermos múltiplos processos, isto vira uma
//      lista de VMAs por espaço de endereço). ----
struct DemandRegion {
    uint64_t base;
    uint64_t size;
    uint64_t flags;
    bool     used;
};
static constexpr int MAX_DEMAND_REGIONS = 16;
static DemandRegion s_demand_regions[MAX_DEMAND_REGIONS];

// ---- Aloca uma tabela de página vazia (zerada) a partir do PFA ----
static pte_t* alloc_table() {
    uint64_t phys = pfa_alloc_frame();
    if (!phys) return nullptr;
    pte_t* table = (pte_t*)phys; // identity-mapped: phys == virt
    for (int i = 0; i < ENTRIES; i++) table[i] = 0;
    return table;
}

// ---- Índices de cada nível a partir de um endereço virtual ----
static inline uint32_t pml4_index(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline uint32_t pdpt_index(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline uint32_t pd_index(uint64_t v)   { return (v >> 21) & 0x1FF; }
static inline uint32_t pt_index(uint64_t v)   { return (v >> 12) & 0x1FF; }

// ---- Converte flags PAGE_* (independentes de arquitetura) para os
//      bits reais de uma entrada de tabela x86_64. ----
static inline pte_t make_entry(uint64_t phys, uint64_t flags) {
    pte_t e = phys & ADDR_MASK;
    e |= (1ULL << 0);                             // Present
    if (flags & PAGE_WRITABLE) e |= (1ULL << 1);  // R/W
    if (flags & PAGE_USER)     e |= (1ULL << 2);  // U/S
    if (flags & PAGE_NO_EXEC)  e |= (1ULL << 63); // XD/NX
    return e;
}

// ---- Habilita o bit NX no EFER (necessário para PAGE_NO_EXEC funcionar) ----
static void enable_nx_bit() {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
    lo |= (1 << 11); // EFER.NXE
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(0xC0000080));
}

static inline void flush_tlb_single(uint64_t virt_addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

static inline void load_cr3(uint64_t phys_pml4) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
}

static inline uint64_t read_cr2() {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

// ---- Garante que existe uma tabela no próximo nível, criando se
//      necessário. As entradas intermediárias (PML4/PDPT/PD) usam
//      sempre RW+User liberado — a restrição real de permissão fica
//      na entrada final (PT), como é o comportamento padrão x86_64:
//      o nível mais restritivo entre os 4 é o que prevalece. ----
static pte_t* get_or_create_table(pte_t* parent, uint32_t index) {
    if (parent[index] & 1) {
        return (pte_t*)(parent[index] & ADDR_MASK);
    }
    pte_t* child = alloc_table();
    if (!child) return nullptr;
    parent[index] = ((uint64_t)child & ADDR_MASK) | 0x7; // P|RW|US nas intermediárias
    return child;
}

// ---- Versões internas que recebem o PML4 explicitamente. Todas as
//      funções públicas (com e sem sufixo _in) convergem para estas
//      — paging_map()/paging_unmap()/paging_translate() (sem
//      sufixo) só repassam s_active_pml4 automaticamente. ----

static bool map_page(pte_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (!pml4) return false;

    pte_t* pdpt = get_or_create_table(pml4, pml4_index(virt_addr));
    if (!pdpt) return false;
    pte_t* pd = get_or_create_table(pdpt, pdpt_index(virt_addr));
    if (!pd) return false;
    pte_t* pt = get_or_create_table(pd, pd_index(virt_addr));
    if (!pt) return false;

    uint32_t idx = pt_index(virt_addr);
    bool was_present = pt[idx] & 1;
    pt[idx] = make_entry(phys_addr, flags | PAGE_PRESENT);

    // invlpg só invalida a TLB da CPU atual para o CR3 ATUALMENTE
    // carregado. Se `pml4` não for o espaço de endereço ativo agora
    // (ex.: process_create montando um processo novo enquanto outro
    // CR3 está ativo), isso é inofensivo (não há entrada de TLB
    // "errada" para invalidar) — a tradução correta vale a partir da
    // próxima vez que ESTE pml4 for carregado em CR3 via
    // paging_switch_to(), que por si só já implica uma TLB limpa
    // para o novo espaço de endereço.
    flush_tlb_single(virt_addr);
    if (!was_present) s_mapped_pages++;
    return true;
}

static void unmap_page(pte_t* pml4, uint64_t virt_addr) {
    if (!pml4) return;
    pte_t* pdpt = (pml4[pml4_index(virt_addr)] & 1) ? (pte_t*)(pml4[pml4_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pdpt) return;
    pte_t* pd = (pdpt[pdpt_index(virt_addr)] & 1) ? (pte_t*)(pdpt[pdpt_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pd) return;
    pte_t* pt = (pd[pd_index(virt_addr)] & 1) ? (pte_t*)(pd[pd_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pt) return;

    uint32_t idx = pt_index(virt_addr);
    if (pt[idx] & 1) {
        pt[idx] = 0;
        flush_tlb_single(virt_addr);
        if (s_mapped_pages) s_mapped_pages--;
    }
}

static bool translate_page(pte_t* pml4, uint64_t virt_addr, uint64_t* out_phys) {
    if (!pml4) return false;
    pte_t* pdpt = (pml4[pml4_index(virt_addr)] & 1) ? (pte_t*)(pml4[pml4_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pdpt) return false;
    pte_t* pd = (pdpt[pdpt_index(virt_addr)] & 1) ? (pte_t*)(pdpt[pdpt_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pd) return false;
    pte_t* pt = (pd[pd_index(virt_addr)] & 1) ? (pte_t*)(pd[pd_index(virt_addr)] & ADDR_MASK) : nullptr;
    if (!pt) return false;

    uint32_t idx = pt_index(virt_addr);
    if (!(pt[idx] & 1)) return false;

    if (out_phys) *out_phys = (pt[idx] & ADDR_MASK) | (virt_addr & (PAGE_SIZE_4K - 1));
    return true;
}

// ---- Identity-mapeia 0..limit_bytes inteiro, granularidade 4KB ----
static void identity_map_range(uint64_t limit_bytes, uint64_t flags) {
    for (uint64_t addr = 0; addr < limit_bytes; addr += PAGE_SIZE_4K) {
        paging_map(addr, addr, flags);
    }
}

// ---- Handler de #PF (vetor 14) registrado em isr.c ----
// Chamado por isr_handler() quando ninguém mais assumiu o vetor 14.
static void page_fault_handler(regs_t* regs) {
    uint64_t fault_addr = read_cr2();
    uint64_t page_base   = fault_addr & ~(PAGE_SIZE_4K - 1);

    // Procura se o endereço cai dentro de alguma região de demand paging
    for (int i = 0; i < MAX_DEMAND_REGIONS; i++) {
        DemandRegion& r = s_demand_regions[i];
        if (!r.used) continue;
        if (fault_addr >= r.base && fault_addr < r.base + r.size) {
            uint64_t phys = pfa_alloc_frame();
            if (!phys) break; // OOM — cai para o panic abaixo

            for (uint64_t i2 = 0; i2 < PAGE_SIZE_4K; i2 += 8)
                *(uint64_t*)(phys + i2) = 0; // zero-fill (identity-mapped)

            if (paging_map(page_base, phys, r.flags)) {
                s_demand_faults++;
                return; // resolvido — a instrução que faltou será re-executada
            }
        }
    }

    // Não é uma falta esperada (região desconhecida, ou permissão
    // violada de propósito — ex.: escrita numa página read-only).
    // Isto SIM é um bug real: reencaminha para o panic padrão de
    // isr.c, que já sabe desenhar a tela detalhada para o vetor 14
    // (decodifica CR2 e o motivo do #PF a partir do error code).
    isr_fatal(regs);
}

} 

//  API pública
extern "C" {

void paging_init(void) {
    for (int i = 0; i < MAX_DEMAND_REGIONS; i++) s_demand_regions[i].used = false;

    enable_nx_bit();

    s_kernel_pml4 = alloc_table();
    // Sem PML4 não há como continuar — isto só falharia com o PFA
    // completamente sem memória, o que já teria quebrado antes.
    if (!s_kernel_pml4) return;
    s_active_pml4 = s_kernel_pml4; // no boot, o espaço ativo é o do kernel

    // Identity-map de 0..4GB com granularidade de 4KB, substituindo
    // o mapeamento "burro" de 2MB do boot.asm. Kernel/dados/heap:
    // RW, sem NX (o código do kernel precisa ser executável).
    // TODO(etapa Ring 3): quando existir separação de seções,
    // marcar .text como somente-execução (sem WRITABLE) e dados
    // como NO_EXEC — por ora priorizamos não quebrar nada em runtime.
    identity_map_range(0x100000000ULL /* 4GB */, PAGE_WRITABLE);

    // ---- BUG CRÍTICO EVITADO AQUI ----
    // Pré-cria (sem mapear nenhuma página de dado ainda, só garante
    // que a TABELA existe) a entrada de PDPT do kernel que cobre a
    // região de pilhas de KERNEL de todas as tasks
    // (TASK_STACK_VREGION_BASE = 0x200000000, ver task.c). Isso tem
    // que acontecer AQUI, antes de qualquer paging_create_address_space()
    // poder ser chamado (Fase 3, processos de usuário) — senão o
    // primeiro processo criado no sistema faria sua cópia da PDPT do
    // kernel ANTES dessa entrada existir (ela só seria criada, sob
    // demanda, na primeira vez que ALGUM processo chamasse
    // task_alloc_kernel_stack_for). O processo receberia uma cópia
    // com essa entrada ZERADA, e quando o kernel (rodando no PML4
    // COMPARTILHADO, ativo no momento de process_create) criasse essa
    // entrada de verdade mais tarde, o PML4/PDPT do processo — já
    // clonado antes — nunca mais veria essa atualização. Resultado:
    // a pilha de kernel do processo existe fisicamente e é válida no
    // PML4 do KERNEL, mas é invisível a partir do PRÓPRIO CR3 do
    // processo — a primeira interrupção ou syscall que precisasse
    // empilhar algo nela (usando RSP0/TSS) causaria um #PF dentro do
    // próprio handler de exceção, sem pilha de exceção alternativa
    // (sem IST configurado) para tratar isso — vira Double Fault e
    // depois Triple Fault, que é fatal e reseta a CPU inteira (visto
    // como "Guru Meditation" no VirtualBox, sem nenhum kernel panic
    // do HAOS aparecer, porque a própria infraestrutura de tratar
    // exceções já estava com a pilha quebrada).
    //
    // Ao garantir que esta entrada de PDPT já existe (mesmo vazia de
    // páginas de dados) ANTES do primeiro paging_create_address_space(),
    // toda cópia de PDPT feita por processos futuros vai sempre pegar
    // um PONTEIRO válido e compartilhado para esta mesma PD — e como
    // temos só uma PD (nível seguinte) compartilhada por todos, as
    // entradas de PT que task_alloc_kernel_stack_for() criar depois
    // (em qualquer momento, para qualquer task) ficam automaticamente
    // visíveis para TODOS os espaços de endereço, presentes e
    // futuros, sem precisar re-sincronizar nada.
    {
        constexpr uint32_t kernel_stack_pdpt_index = (uint32_t)((HAOS_KERNEL_STACK_VREGION_BASE >> 30) & 0x1FF);
        get_or_create_table(s_kernel_pml4, 0); // garante o PDPT do índice 0 (todo o
                                                  // espaço baixo deste projeto — identity-map,
                                                  // pilhas de kernel, HEXEC — vive aqui)
        pte_t* pdpt0 = (pte_t*)(s_kernel_pml4[0] & ADDR_MASK);
        get_or_create_table(pdpt0, kernel_stack_pdpt_index); // garante a PD da região de pilhas de kernel
    }

    load_cr3((uint64_t)s_kernel_pml4);
    isr_register_handler(14, page_fault_handler);
}

bool paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    return map_page(s_active_pml4, virt_addr, phys_addr, flags);
}

void paging_unmap(uint64_t virt_addr) {
    unmap_page(s_active_pml4, virt_addr);
}

bool paging_translate(uint64_t virt_addr, uint64_t* out_phys) {
    return translate_page(s_active_pml4, virt_addr, out_phys);
}

bool paging_map_in(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    pte_t* pml4 = as ? (pte_t*)as : s_active_pml4; // NULL = espaço ativo agora
    return map_page(pml4, virt_addr, phys_addr, flags);
}

void paging_unmap_in(address_space_t* as, uint64_t virt_addr) {
    pte_t* pml4 = as ? (pte_t*)as : s_active_pml4;
    unmap_page(pml4, virt_addr);
}

bool paging_translate_in(address_space_t* as, uint64_t virt_addr, uint64_t* out_phys) {
    pte_t* pml4 = as ? (pte_t*)as : s_active_pml4;
    return translate_page(pml4, virt_addr, out_phys);
}

address_space_t* paging_create_address_space(void) {
    pte_t* pml4 = alloc_table(); // já vem zerado (alloc_table zera a página)
    if (!pml4) return nullptr;

    // ---- CORREÇÃO DE DESIGN IMPORTANTE ----
    // A ideia original era "compartilhar a metade ALTA do PML4
    // (índices 256-511) e isolar a metade BAIXA (0-255)", inspirada
    // no esquema clássico de higher-half kernel. Mas este projeto
    // NÃO usa higher-half: identity-map (0..4GB), pilhas de kernel
    // (0x200000000) e a região de carga de executáveis
    // (HEXEC_LOAD_ADDR, 0x300000000) são TODOS endereços "baixos" —
    // caem todos no MESMO índice de PML4 (índice 0), só em índices
    // de PDPT diferentes (0-3 para identity-map, 8 para pilhas de
    // kernel, 12 para HEXEC). Copiar por "metade do PML4" faria
    // pml4[0] inteiro (contendo TUDO isso) ser tratado como uma
    // única unidade — impossível separar "kernel compartilhado" de
    // "usuário exclusivo" nesse nível.
    //
    // A correção: copiamos pml4[0] do kernel (a única entrada de
    // PML4 usada neste projeto), mas com uma tabela PDPT PRÓPRIA
    // deste processo, não a mesma referência do kernel. Nessa PDPT
    // nova, copiamos as entradas que devem continuar compartilhadas
    // (identity-map do kernel, índices 0-3; a REGIÃO de pilhas de
    // kernel, índice 8 — necessária porque RSP0/TSS aponta para lá e
    // precisa ser endereçável não importa qual CR3 esteja ativo
    // quando uma interrupção acontece) e deixamos ZERADA a entrada
    // que cobre a região de carga de executáveis (índice 12,
    // HEXEC_LOAD_ADDR e a pilha de usuário) — essa cada processo
    // cria a sua própria, sob demanda, ao chamar paging_map_in()
    // durante o carregamento (get_or_create_table aloca uma PD nova
    // ali dentro, exclusiva deste address_space).
    pte_t* kernel_pdpt = (pte_t*)(s_kernel_pml4[0] & ADDR_MASK);
    pte_t* proc_pdpt = alloc_table();
    if (!proc_pdpt) { pfa_free_frame((uint64_t)pml4); return nullptr; }

    static constexpr uint32_t HEXEC_PDPT_INDEX = (HEXEC_LOAD_ADDR >> 30) & 0x1FF; // ver hexec.h/process.c
    for (int i = 0; i < ENTRIES; i++) {
        if ((uint32_t)i == HEXEC_PDPT_INDEX) continue; // exclusivo do processo
        proc_pdpt[i] = kernel_pdpt[i];
    }

    pml4[0] = ((uint64_t)proc_pdpt & ADDR_MASK) | 0x7; // P|RW|US
    return (address_space_t*)pml4;
}

void paging_destroy_address_space(address_space_t* as) {
    pte_t* pml4 = (pte_t*)as;
    if (!pml4 || pml4 == s_kernel_pml4) return; // nunca destrói o do kernel

    // Layout criado por paging_create_address_space(): pml4[0] aponta
    // para uma PDPT PRÓPRIA deste processo, cujas entradas são todas
    // herdadas do kernel (identity-map, pilha de kernel, etc.) EXCETO
    // a entrada HEXEC_PDPT_INDEX, que é exclusiva (contém a PD/PT da
    // imagem do executável e da pilha de usuário deste processo).
    //
    // Por isso, ao destruir: (1) liberamos a PD e PTs sob
    // HEXEC_PDPT_INDEX de verdade (são exclusivas), (2) NÃO tocamos
    // em nenhuma outra entrada da PDPT (são as mesmas do kernel —
    // liberar aqui derrubaria o identity-map ou as pilhas de kernel
    // de TODAS as tasks do sistema), e (3) liberamos a própria PDPT
    // (ela sim é exclusiva deste processo, mesmo que várias de suas
    // entradas apontem para PDs compartilhados) e o PML4.
    static constexpr uint32_t HEXEC_PDPT_INDEX = (HEXEC_LOAD_ADDR >> 30) & 0x1FF;

    if (pml4[0] & 1) {
        pte_t* pdpt = (pte_t*)(pml4[0] & ADDR_MASK);

        if (pdpt[HEXEC_PDPT_INDEX] & 1) {
            pte_t* pd = (pte_t*)(pdpt[HEXEC_PDPT_INDEX] & ADDR_MASK);
            for (int i2 = 0; i2 < ENTRIES; i2++) {
                if (!(pd[i2] & 1)) continue;
                pte_t* pt = (pte_t*)(pd[i2] & ADDR_MASK);
                // As páginas de DADOS apontadas por `pt` (código,
                // pilha de usuário do processo) já devem ter sido
                // liberadas por task_free_mapped_pages() ANTES desta
                // chamada — ver o reaper em task.c. Aqui só
                // devolvemos ao PFA a própria tabela PT.
                pfa_free_frame((uint64_t)pt);
            }
            pfa_free_frame((uint64_t)pd);
        }

        pfa_free_frame((uint64_t)pdpt);
    }
    pfa_free_frame((uint64_t)pml4);
}

void paging_switch_to(address_space_t* as) {
    pte_t* pml4 = as ? (pte_t*)as : s_kernel_pml4;
    if (pml4 == s_active_pml4) return; // já é o ativo — evita um
                                          // load_cr3 desnecessário,
                                          // que sozinho já invalida a
                                          // TLB inteira (custoso).
    s_active_pml4 = pml4;
    load_cr3((uint64_t)pml4);
}

uint64_t paging_address_space_cr3(address_space_t* as) {
    pte_t* pml4 = as ? (pte_t*)as : s_kernel_pml4;
    return (uint64_t)pml4;
}

bool paging_reserve_region(uint64_t virt_addr, uint64_t size, uint64_t flags) {
    for (int i = 0; i < MAX_DEMAND_REGIONS; i++) {
        if (!s_demand_regions[i].used) {
            s_demand_regions[i] = { virt_addr, size, flags, true };
            return true;
        }
    }
    return false; // tabela de regiões cheia — aumentar MAX_DEMAND_REGIONS se necessário
}

uint64_t paging_get_mapped_pages(void)  { return s_mapped_pages; }
uint64_t paging_get_demand_faults(void) { return s_demand_faults; }

} 
