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
#include "types.h"
#include "../drivers/fb.h"

namespace {

// ---- Entrada de tabela de página (formato x86_64 padrão) ----
using pte_t = uint64_t;

static constexpr uint64_t ADDR_MASK   = 0x000FFFFFFFFFF000ULL; // bits 12-51
static constexpr int      ENTRIES     = 512;

static pte_t* s_pml4 = nullptr;

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

    s_pml4 = alloc_table();
    // Sem PML4 não há como continuar — isto só falharia com o PFA
    // completamente sem memória, o que já teria quebrado antes.
    if (!s_pml4) return;

    // Identity-map de 0..4GB com granularidade de 4KB, substituindo
    // o mapeamento "burro" de 2MB do boot.asm. Kernel/dados/heap:
    // RW, sem NX (o código do kernel precisa ser executável).
    // TODO(etapa Ring 3): quando existir separação de seções,
    // marcar .text como somente-execução (sem WRITABLE) e dados
    // como NO_EXEC — por ora priorizamos não quebrar nada em runtime.
    identity_map_range(0x100000000ULL /* 4GB */, PAGE_WRITABLE);

    load_cr3((uint64_t)s_pml4);
    isr_register_handler(14, page_fault_handler);
}

bool paging_map(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if (!s_pml4) return false;

    pte_t* pdpt = get_or_create_table(s_pml4, pml4_index(virt_addr));
    if (!pdpt) return false;
    pte_t* pd = get_or_create_table(pdpt, pdpt_index(virt_addr));
    if (!pd) return false;
    pte_t* pt = get_or_create_table(pd, pd_index(virt_addr));
    if (!pt) return false;

    uint32_t idx = pt_index(virt_addr);
    bool was_present = pt[idx] & 1;
    pt[idx] = make_entry(phys_addr, flags | PAGE_PRESENT);

    flush_tlb_single(virt_addr);
    if (!was_present) s_mapped_pages++;
    return true;
}

void paging_unmap(uint64_t virt_addr) {
    if (!s_pml4) return;
    pte_t* pdpt = (s_pml4[pml4_index(virt_addr)] & 1) ? (pte_t*)(s_pml4[pml4_index(virt_addr)] & ADDR_MASK) : nullptr;
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

bool paging_translate(uint64_t virt_addr, uint64_t* out_phys) {
    if (!s_pml4) return false;
    pte_t* pdpt = (s_pml4[pml4_index(virt_addr)] & 1) ? (pte_t*)(s_pml4[pml4_index(virt_addr)] & ADDR_MASK) : nullptr;
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
