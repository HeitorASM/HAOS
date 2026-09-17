// ============================================================
//  kernel/syscall.c — Dispatcher de syscalls + configuração dos MSRs
//
//  Duas syscalls mínimas nesta etapa, suficientes para o processo
//  de teste provar a pilha inteira (paginação de usuário + TSS +
//  Ring 3 + syscall) funcionando de ponta a ponta:
//
//    SYS_WRITE(ptr, len) — copia len bytes de uma página de USUÁRIO
//                          para um buffer de kernel e desenha na tela.
//                          Não confia em ptr vindo de Ring 3 sem
//                          validação (ver validate_user_ptr abaixo).
//    SYS_EXIT(code)      — por enquanto, sem scheduler, "encerrar
//                          processo" só pode significar halt: não
//                          há para onde voltar. Isso muda quando
//                          houver um scheduler de verdade.
// ============================================================

#include "syscall.h"
#include "gdt.h"
#include "types.h"
#include "memory.h"
#include "paging.h"
#include "task.h"

extern void syscall_entry(void); // definido em syscall_asm.asm

// ---- MSRs usados pelo mecanismo SYSCALL/SYSRET ----
#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void syscall_init(void) {
    // EFER.SCE (bit 0) — habilita as instruções SYSCALL/SYSRET.
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1ULL;
    wrmsr(MSR_EFER, efer);

    // STAR[63:48] = base usada pelo SYSRET para calcular os seletores
    //               de USUÁRIO (fórmula do Intel/AMD SDM):
    //                 SS_ring3 = STAR[63:48] + 8   (RPL forçado a 3)
    //                 CS_ring3 = STAR[63:48] + 16  (RPL forçado a 3)
    //               No nosso layout de GDT (gdt.h):
    //                 0x10 = kernel data, 0x18 = user data, 0x20 = user code
    //               Ou seja, user_data está 8 bytes acima de kernel_data,
    //               e user_code está 16 bytes acima — exatamente o que a
    //               fórmula do SYSRET espera. Por isso a base é GDT_KERNEL_DATA:
    //                 GDT_KERNEL_DATA + 8  = 0x18 = GDT_USER_DATA  ✓
    //                 GDT_KERNEL_DATA + 16 = 0x20 = GDT_USER_CODE  ✓
    uint64_t star_user_base = GDT_KERNEL_DATA;

    // STAR[47:32] = seletor de código de KERNEL usado no momento da
    //               entrada (SYSCALL): CS_kernel = valor, SS_kernel = valor+8.
    //               GDT_KERNEL_CODE=0x08, GDT_KERNEL_DATA=0x10 = 0x08+8 ✓
    uint64_t star_kernel_base = GDT_KERNEL_CODE;

    uint64_t star = (star_user_base << 48) | (star_kernel_base << 32);
    wrmsr(MSR_STAR, star);

    // LSTAR — endereço do ponto de entrada (syscall_entry, em asm).
    wrmsr(MSR_LSTAR, (uint64_t)&syscall_entry);

    // SFMASK — bits de RFLAGS a serem ZERADOS na entrada da syscall.
    // Mascaramos IF (bit 9) para não sermos interrompidos antes de
    // trocar para a stack de kernel, e DF (bit 10) porque a ABI
    // exige DF=0 dentro de qualquer código C.
    wrmsr(MSR_SFMASK, (1ULL << 9) | (1ULL << 10));
}

// ---- Validação de ponteiro vindo de Ring 3 ----
// Um processo de usuário pode passar QUALQUER valor em rdi/rsi —
// inclusive endereços de memória de kernel. Nunca confie neles sem
// checar que (a) a página está de fato mapeada e (b) foi mapeada
// com PAGE_USER (ou seja, o próprio processo tinha permissão de
// acessá-la). Sem isso, sys_write vira uma forma de ler memória de
// kernel arbitrária a partir de Ring 3 — uma vulnerabilidade clássica.
static bool validate_user_range(uint64_t addr, uint64_t len) {
    if (len == 0) return true;
    uint64_t start_page = addr & ~(PAGE_SIZE_4K - 1);
    uint64_t end_page    = (addr + len - 1) & ~(PAGE_SIZE_4K - 1);

    for (uint64_t page = start_page; page <= end_page; page += PAGE_SIZE_4K) {
        uint64_t phys;
        if (!paging_translate(page, &phys)) return false; // não mapeada
        // TODO: quando paging_translate expuser as flags da entrada
        // (não só o endereço físico), checar aqui também o bit
        // PAGE_USER — por ora, qualquer página mapeada é aceita,
        // o que é suficiente para o teste desta etapa mas deve ser
        // reforçado antes de expor isso a processos não-confiáveis.
    }
    return true;
}

// ---- SYS_WRITE(ptr, len) ----
// Copia os bytes recebidos para o canal de saída (stdout) da task
// atual (ver task_stdout_write em task.c), em vez de desenhar
// direto no framebuffer global. O terminal que lançou este processo
// (task->stdout_owner) drena esse buffer a cada frame do seu loop
// de eventos e imprime como texto normal — isso resolve dois
// problemas da abordagem anterior: (1) a saída não competia mais
// visualmente com o redesenho da GUI (que fazia o texto "piscar e
// sumir", já que o desktop redesenha o framebuffer inteiro a cada
// frame); (2) a saída agora tem um dono claro (o terminal que
// lançou o processo), abrindo caminho para múltiplos terminais/
// processos coexistindo sem escreverem uns por cima dos outros.
#define SYS_WRITE_MAX_LEN 256

static uint64_t sys_write(uint64_t user_ptr, uint64_t len) {
    if (len > SYS_WRITE_MAX_LEN) len = SYS_WRITE_MAX_LEN;
    if (!validate_user_range(user_ptr, len)) {
        return (uint64_t)-1; // endereço inválido/não mapeado — recusa
    }

    uint8_t buf[SYS_WRITE_MAX_LEN];
    kmemcpy(buf, (const void*)user_ptr, len);

    task_t* t = task_current();
    task_stdout_write(t, buf, len);
    return len;
}

// ---- SYS_EXIT(code) ----
// Encerra o processo de usuário atual chamando task_exit(), que
// marca a task como ZOMBIE e força uma troca de contexto imediata
// para a próxima task pronta — o scheduler assume o controle da
// CPU de verdade a partir daqui, em vez do "hlt" que esta syscall
// fazia antes do scheduler existir (quando não havia para onde
// voltar). A pilha/imagem deste processo são liberadas depois,
// pelo reaper (ver task_reap_zombies), não aqui.
//
// ATENÇÃO — por que isto é seguro apesar de syscall_entry usar uma
// ÚNICA pilha de kernel global (syscall_stack, em syscall_asm.asm)
// compartilhada por TODAS as syscalls de TODAS as tasks: task_exit()
// aciona um task_switch() que empilha o contexto callee-saved desta
// task EM CIMA dessa mesma syscall_stack global, não na pilha de
// kernel própria da task (essa só é usada para IRQs via TSS.RSP0,
// não para o corpo de uma syscall). Isso só não corrompe nada porque
// esta task nunca mais vai retornar para cima dessa pilha (está
// TASK_ZOMBIE) — nenhuma outra syscall vai rodar "por baixo" desse
// contexto empilhado antes dele se tornar irrelevante. QUALQUER
// outra syscall que um dia precise chamar scheduler_yield()/
// task_switch() SEM ser um caminho sem-retorno como este teria que
// primeiro resolver isso (dar a cada task sua própria pilha de
// syscall, não uma global) — não fazer isso silenciosamente
// corromperia o estado de outras tasks.
static _Noreturn uint64_t sys_exit(uint64_t code) {
    task_t* t = task_current();
    // Marcado ANTES de task_exit(): o terminal dono (stdout_owner)
    // observa has_exited para mostrar "processo encerrado (código X)"
    // uma única vez, mesmo enquanto a task ainda está TASK_ZOMBIE
    // aguardando o reaper — sem isso, o terminal não teria como saber
    // que o processo terminou até o slot inteiro ser reciclado.
    t->exit_code  = code;
    t->has_exited = true;
    task_exit(); // nunca retorna
    __builtin_unreachable();
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5; // não usados pelas syscalls atuais

    switch (num) {
        case SYS_WRITE: return sys_write(a1, a2);
        case SYS_EXIT:  return sys_exit(a1); // não retorna
        default:        return (uint64_t)-1; // syscall desconhecida
    }
}
