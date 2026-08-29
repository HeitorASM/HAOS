// ============================================================
//  Antes deste módulo, qualquer exceção (divisão por zero,
//  acesso inválido à memória, opcode inválido, etc.) caía no
//  default_handler genérico e o sistema seguia rodando em
//  estado indefinido — ou travava sem explicação nenhuma.
//
//  Agora cada uma das 32 exceções da CPU tem um stub próprio
//  (isr_asm.asm) que salva o contexto completo e chama
//  isr_handler() aqui. Se ninguém registrou um handler
//  específico para aquele vetor, entramos num panic screen
//  com toda a informação necessária para depurar.
// ============================================================

#include "isr.h"
#include "idt.h"
#include "types.h"
#include "memory.h"
#include "../drivers/fb.h"

// Declaração dos 32 stubs definidos em isr_asm.asm
#define DECL_ISR(n) extern void isr##n(void);
DECL_ISR(0)  DECL_ISR(1)  DECL_ISR(2)  DECL_ISR(3)
DECL_ISR(4)  DECL_ISR(5)  DECL_ISR(6)  DECL_ISR(7)
DECL_ISR(8)  DECL_ISR(9)  DECL_ISR(10) DECL_ISR(11)
DECL_ISR(12) DECL_ISR(13) DECL_ISR(14) DECL_ISR(15)
DECL_ISR(16) DECL_ISR(17) DECL_ISR(18) DECL_ISR(19)
DECL_ISR(20) DECL_ISR(21) DECL_ISR(22) DECL_ISR(23)
DECL_ISR(24) DECL_ISR(25) DECL_ISR(26) DECL_ISR(27)
DECL_ISR(28) DECL_ISR(29) DECL_ISR(30) DECL_ISR(31)
#undef DECL_ISR

// idt_set_gate é uma pequena extensão de idt.c (ver idt.h/idt.c)
// que permite instalar um handler com selector/flags customizados
// além dos 3 vetores de hardware já existentes.
extern void idt_set_gate(int n, void (*handler)(void));

// Tabela de handlers customizados por vetor. NULL = usa o panic padrão.
static isr_handler_fn s_custom_handlers[32];

// Nomes oficiais das 32 exceções x86_64 (Intel SDM Vol.3 cap.6)
static const char* const s_exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug Exception",
    "NMI Non-Maskable Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available (FPU)",
    "#DF Double Fault",
    "Coprocessor Segment Overrun (legado)",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection Fault",
    "#PF Page Fault",
    "Reservado (15)",
    "#MF x87 FPU Floating-Point Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point Exception",
    "#VE Virtualization Exception",
    "#CP Control Protection Exception",
    "Reservado (22)", "Reservado (23)", "Reservado (24)",
    "Reservado (25)", "Reservado (26)", "Reservado (27)", "Reservado (28)",
    "#VC VMM Communication Exception",
    "#SX Security Exception",
    "Reservado (31)",
};

void isr_install(void) {
    void (*stubs[32])(void) = {
        isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
        isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    };
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, stubs[i]);
        s_custom_handlers[i] = 0;
    }
}

void isr_register_handler(uint8_t vector, isr_handler_fn fn) {
    if (vector < 32) s_custom_handlers[vector] = fn;
}

// ---- CR2: endereço que causou o Page Fault (só válido no vetor 14) ----
static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

// ---- Desenha "label: 0xHEX" numa linha, avançando o cursor Y ----
static uint32_t s_panic_y;
static void panic_line(const char* label, uint64_t value) {
    char buf[24];
    fb_draw_string(30, s_panic_y, label, 0x9FB6D9, 0x1A0000, false);
    kuitoa_hex(value, buf);
    fb_draw_string(220, s_panic_y, buf, COLOR_WHITE, 0x1A0000, false);
    s_panic_y += 20;
}

// Decodifica os bits do error code do #PF (vetor 14) num texto curto.
static void pf_reason(uint64_t err, char* out) {
    const char* present = (err & 1)      ? "protecao"      : "pagina ausente";
    const char* rw       = (err & 2)      ? "escrita"       : "leitura";
    const char* mode      = (err & 4)      ? "usuario"       : "kernel";
    const char* reserved  = (err & 8)      ? " [reserved-bit violado]" : "";
    const char* ifetch    = (err & 16)     ? " [busca de instrucao]"   : "";
    kstrcpy(out, present);
    kstrcat(out, " / ");
    kstrcat(out, rw);
    kstrcat(out, " / modo ");
    kstrcat(out, mode);
    kstrcat(out, reserved);
    kstrcat(out, ifetch);
}

// Tela de panic detalhada — chamada quando não há handler custom
// registrado para o vetor. Não usa o shadow buffer (a estrutura de
// double-buffering pode estar corrompida); escreve direto no
// framebuffer real para maximizar a chance de a tela aparecer.
static void isr_panic_screen(regs_t* r) {
    const char* name = (r->vector < 32) ? s_exception_names[r->vector] : "Vetor desconhecido";

    fb_clear(0x1A0000);
    fb_draw_string(30, 24, "!!! KERNEL PANIC — EXCECAO DA CPU !!!", COLOR_WHITE, 0x1A0000, false);
    fb_draw_string(30, 50, name, 0xFF8888, 0x1A0000, false);

    s_panic_y = 90;
    panic_line("Vetor:",       r->vector);
    panic_line("Error Code:",  r->error_code);
    panic_line("RIP:",         r->rip);
    panic_line("CS:",          r->cs);
    panic_line("RFLAGS:",      r->rflags);
    panic_line("RSP:",         r->rsp);
    panic_line("SS:",          r->ss);

    if (r->vector == 14) { // #PF — informação extra crucial
        uint64_t fault_addr = read_cr2();
        panic_line("CR2 (endereco):", fault_addr);
        char reason[96];
        pf_reason(r->error_code, reason);
        fb_draw_string(30, s_panic_y, "Motivo:", 0x9FB6D9, 0x1A0000, false);
        fb_draw_string(220, s_panic_y, reason, COLOR_WHITE, 0x1A0000, false);
        s_panic_y += 20;
    }

    s_panic_y += 10;
    fb_draw_string(30, s_panic_y, "-- Registradores de proposito geral --", 0x7799BB, 0x1A0000, false);
    s_panic_y += 22;
    panic_line("RAX:", r->rax);  panic_line("RBX:", r->rbx);
    panic_line("RCX:", r->rcx);  panic_line("RDX:", r->rdx);
    panic_line("RSI:", r->rsi);  panic_line("RDI:", r->rdi);
    panic_line("RBP:", r->rbp);
    panic_line("R8: ", r->r8);   panic_line("R9: ", r->r9);
    panic_line("R10:", r->r10);  panic_line("R11:", r->r11);
    panic_line("R12:", r->r12);  panic_line("R13:", r->r13);
    panic_line("R14:", r->r14);  panic_line("R15:", r->r15);

    __asm__ volatile ("cli");
    while (1) __asm__ volatile ("hlt");
}

void isr_handler(regs_t* regs) {
    uint8_t v = (uint8_t)regs->vector;

    if (v < 32 && s_custom_handlers[v]) {
        s_custom_handlers[v](regs);
        return;
    }

    // Exceções de depuração não-fatais poderiam, no futuro, apenas
    // logar e continuar (ex.: #BP para breakpoints de um debugger).
    // Por ora, toda exceção sem handler custom é tratada como fatal,
    // porque HAOS ainda roda inteiramente em Ring 0 sem isolamento:
    // deixar a execução continuar depois de uma falta é mais perigoso
    // do que parar com um diagnóstico claro.
    isr_panic_screen(regs);
}
