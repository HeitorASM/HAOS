#include "gdt.h"
#include "types.h"

// ============================================================
//
//  Antes: 3 entradas (null, kernel code, kernel data). Suficiente
//  enquanto tudo rodava em Ring 0.
//
//  Agora: 5 descriptors "normais" de 8 bytes + 1 descriptor de
//  TSS de 16 bytes (TSS de 64-bit é maior porque guarda RSP0-2 e
//  IST1-7 como ponteiros de 64-bit inteiros, não 32-bit como no
//  formato legado de 32-bit).
//
//  A TSS em modo long não é mais usada para trocar de tarefa via
//  hardware (isso não existe mais em 64-bit) — ela serve só para
//  guardar RSP0: o stack pointer que a CPU carrega AUTOMATICAMENTE
//  sempre que uma interrupção ocorre vindo de Ring 3. Sem isso,
//  uma interrupção em Ring 3 usaria o RSP do processo de usuário
//  para empilhar o frame de interrupção — inseguro e, em geral,
//  quebrado (o processo de usuário nem deveria poder ver esse
//  stack de kernel).
// ============================================================

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_hi;
    uint8_t  base_hi;
} __attribute__((packed));

// Descriptor de TSS de 64-bit: 16 bytes (ocupa 2 slots de gdt_entry).
// base é um ponteiro de 64-bit inteiro, por isso os campos extras.
struct tss_descriptor {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_hi;
    uint8_t  base_hi;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

// Estrutura da TSS de 64-bit (Intel SDM Vol.3, seção 8.7).
// Só usamos rsp0 por enquanto; ist1-7 ficam reservados para uso
// futuro (ex.: pilha dedicada para Double Fault, importante quando
// tivermos múltiplos processos e uma pilha de kernel puder estourar).
struct tss_64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// 5 descriptors normais (null, kcode, kdata, udata, ucode) +
// 2 slots para o descriptor de TSS (16 bytes = 2x gdt_entry).
static struct gdt_entry gdt[7];
static struct gdt_ptr   gdt_ptr;
static struct tss_64    tss;

// Stack de kernel dedicada para quando uma interrupção ocorrer
// vindo de Ring 3. 16KB é generoso para handlers de exceção/IRQ,
// que não devem ter recursão profunda nem alocar buffers grandes
// na própria pilha.
#define KERNEL_STACK_SIZE 16384
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags) {
    gdt[i].limit_lo       = limit & 0xFFFF;
    gdt[i].base_lo        = base  & 0xFFFF;
    gdt[i].base_mid       = (base  >> 16) & 0xFF;
    gdt[i].access         = access;
    gdt[i].flags_limit_hi = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_hi        = (base  >> 24) & 0xFF;
}

// Preenche o descriptor de TSS de 16 bytes (ocupa os índices 5 e 6
// de gdt[], reinterpretados como um único tss_descriptor).
static void gdt_set_tss(int i, uint64_t base, uint32_t limit) {
    struct tss_descriptor* desc = (struct tss_descriptor*)&gdt[i];
    desc->limit_lo       = limit & 0xFFFF;
    desc->base_lo        = base & 0xFFFF;
    desc->base_mid       = (base >> 16) & 0xFF;
    desc->access         = 0x89; // P=1 DPL=0 Type=1001 (TSS disponível, 64-bit)
    desc->flags_limit_hi = (limit >> 16) & 0x0F;
    desc->base_hi        = (base >> 24) & 0xFF;
    desc->base_upper32   = (base >> 32) & 0xFFFFFFFF;
    desc->reserved        = 0;
}

// Definido em gdt_asm.asm
extern void gdt_flush(uint64_t ptr);
extern void tss_flush(void);

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    gdt_set(0, 0, 0x00000, 0x00, 0x0); // null

    // ---- Segmentos de kernel (Ring 0) — inalterados da versão anterior ----
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0x2); // kernel code: P=1 DPL=0 E=1 L=1
    gdt_set(2, 0, 0xFFFFF, 0x92, 0x0); // kernel data: P=1 DPL=0 W=1

    // ---- Segmentos de usuário (Ring 3) ----
    // Em modo long, base/limit dos descriptors de código/dados são
    // ignorados pela CPU (segmentação "flat" obrigatória) — só os
    // bits de DPL/tipo/long-mode importam. Ainda assim preenchemos
    // base=0/limit=0xFFFFF por clareza e compatibilidade com
    // ferramentas que inspecionam a GDT.
    gdt_set(3, 0, 0xFFFFF, 0xF2, 0x0); // user data: P=1 DPL=3 W=1
    gdt_set(4, 0, 0xFFFFF, 0xFA, 0x2); // user code: P=1 DPL=3 E=1 L=1

    // Zera a TSS inteira e configura RSP0 com o topo da stack de
    // kernel dedicada. iomap_base = sizeof(tss) desabilita o mapa
    // de permissão de I/O por porta (nenhuma porta liberada para
    // Ring 3 — todo acesso a hardware por processos de usuário
    // deve passar por syscall, nunca direto).
    for (uint64_t i = 0; i < sizeof(tss); i++) ((uint8_t*)&tss)[i] = 0;
    tss.rsp0        = (uint64_t)(kernel_stack + KERNEL_STACK_SIZE);
    tss.iomap_base  = sizeof(tss);

    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss) - 1);

    gdt_flush((uint64_t)&gdt_ptr);
    tss_flush();
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
