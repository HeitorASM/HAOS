// ============================================================
//  kernel/process.c — Loader de executáveis HEXEC + processos
// ============================================================
#include "process.h"
#include "hexec.h"
#include "memory.h"
#include "paging.h"
#include "usermode.h"
#include "gdt.h"
#include "../fs/vfs.h"

// Trampolim em ASM (task_switch.asm) que, na primeira vez que um
// processo de usuário é escalonado, lê user_entry/user_stack_top
// da própria task atual e chama enter_usermode() — nunca retorna
// (enter_usermode faz IRETQ para Ring 3).
extern void user_bootstrap_trampoline(void);

// Endereço virtual dedicado às pilhas de USUÁRIO — o MESMO endereço
// para todo processo, o que agora é seguro (Fase 3): cada processo
// tem seu próprio address_space_t (PML4 com metade de usuário
// isolada), então "0x300100000" em um processo é uma tradução
// completamente independente de "0x300100000" em outro — não há
// mais colisão real de memória física, só uma coincidência de
// numeração de endereço virtual, que é inofensiva.
//
// IMPORTANTE — dependência com paging_create_address_space(): este
// endereço PRECISA cair na mesma entrada de PDPT (bits 30-38) que
// HEXEC_LOAD_ADDR (ver hexec.h), porque é exatamente essa entrada de
// PDPT que paging_create_address_space() deixa de fora da cópia
// herdada do kernel — só ela é exclusiva/isolada por processo. Se
// este endereço for alterado no futuro para uma faixa que caia numa
// entrada de PDPT DIFERENTE de HEXEC_LOAD_ADDR, a pilha de usuário
// passaria a viver numa região COMPARTILHADA com o kernel (herdada,
// não isolada) — quebrando o isolamento entre processos de forma
// silenciosa, sem nenhum erro de compilação ou crash óbvio. Hoje
// 0x300000000 (HEXEC) e 0x300100000 (pilha) diferem por só 1MB,
// bem dentro do 1GB coberto pela mesma entrada de PDPT — mas ao
// mudar qualquer um dos dois, confira pdpt_index(x) = (x>>30)&0x1FF
// para os dois continuarem batendo.
#define USER_STACK_VREGION_BASE  0x300100000ULL

// Lê o header HEXEC do início de um arquivo já carregado em memória
// pelo VFS (node->data, node->size) e valida magic/version/tamanhos
// contra o tamanho real do arquivo. Retorna false se qualquer coisa
// não bater — nunca confiamos cegamente em bytes vindos de um
// arquivo (mesmo sendo "nosso" formato, um arquivo corrompido ou
// truncado não pode virar um crash ou um mapeamento de memória
// além do que o arquivo realmente contém).
static bool parse_hexec_header(VfsNode* file, hexec_header_t* out_hdr) {
    if (!file || file->type != VFS_FILE) return false;
    if (file->size < sizeof(hexec_header_t)) return false;

    kmemcpy(out_hdr, file->data, sizeof(hexec_header_t));

    if (out_hdr->magic != HEXEC_MAGIC) return false;
    if (out_hdr->version != HEXEC_VERSION) return false;

    uint64_t available = file->size - sizeof(hexec_header_t);
    if (out_hdr->code_size > available) return false;

    uint64_t image_total = out_hdr->code_size + out_hdr->bss_size;
    if (out_hdr->entry_offset >= image_total) return false;

    return true;
}

// Mapeia `pages` páginas físicas novas e zeradas a partir de
// `virt_start`, DENTRO do espaço de endereço `as` (que pode não
// estar ativo no momento — é exatamente o caso normal ao montar um
// processo novo). Em caso de falha no meio, desfaz tudo que já
// mapeou e retorna false — mesma disciplina de "nunca vazar página
// física parcial" usada em task.c.
//
// O zero-fill e qualquer cópia de dados (ver process_create) usam o
// endereço FÍSICO de cada página (que é sempre acessível via
// identity-map, phys==virt, independente de qual espaço de endereço
// está ativo agora) em vez do endereço virtual de destino no
// processo — porque esse endereço virtual só existe no PML4 de `as`,
// que ainda não está carregado em CR3 neste momento. Escrever
// diretamente em (void*)virt_addr aqui causaria um #PF imediato.
static bool map_fresh_zeroed_pages_in(address_space_t* as, uint64_t virt_start,
                                       uint64_t pages, uint64_t flags,
                                       uint64_t* out_first_phys /* opcional */) {
    uint64_t mapped = 0;
    uint64_t first_phys = 0;
    for (; mapped < pages; mapped++) {
        uint64_t phys = pfa_alloc_frame();
        if (phys == 0) break;
        if (mapped == 0) first_phys = phys;

        uint64_t va = virt_start + mapped * PAGE_SIZE_4K;
        if (!paging_map_in(as, va, phys, flags)) {
            pfa_free_frame(phys);
            break;
        }
        // Zero-fill via endereço FÍSICO (identity-mapped no espaço do
        // kernel) — nunca via `va`, que só é válido depois que `as`
        // estiver ativo em CR3.
        kmemset((void*)phys, 0, PAGE_SIZE_4K);
    }
    if (mapped < pages) {
        task_free_mapped_pages(as, virt_start, mapped);
        return false;
    }
    if (out_first_phys) *out_first_phys = first_phys;
    return true;
}

static uint64_t pages_needed_for(uint64_t bytes) {
    return (bytes + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
}

task_t* process_create(const char* name, VfsNode* executable_file) {
    hexec_header_t hdr;
    if (!parse_hexec_header(executable_file, &hdr)) return NULL;

    // ---- reserva o slot de task ANTES de mexer em qualquer página,
    // para que uma tabela de tasks cheia falhe rápido sem ter
    // mapeado nada ainda ----
    task_t* t = task_reserve_slot();
    if (!t) return NULL;

    // ---- cria o espaço de endereço isolado deste processo (Fase 3) ----
    // A partir daqui, este processo tem seu PRÓPRIO PML4 — sua
    // metade de usuário (código, dados, pilha) é invisível para
    // qualquer outro processo e vice-versa. A metade de kernel é
    // compartilhada (mesmas tabelas do PML4 do kernel), então
    // syscalls e o próprio kernel continuam enxergando tudo
    // normalmente independente de qual processo está ativo.
    address_space_t* as = paging_create_address_space();
    if (!as) {
        t->state = TASK_UNUSED;
        return NULL;
    }

    uint64_t image_pages = pages_needed_for(hdr.code_size + hdr.bss_size);
    if (image_pages == 0) image_pages = 1;

    // ---- mapeia a imagem (código+dados+bss) em HEXEC_LOAD_ADDR,
    // DENTRO do espaço de endereço do processo ----
    uint64_t image_first_phys = 0;
    if (!map_fresh_zeroed_pages_in(as, HEXEC_LOAD_ADDR, image_pages,
                                    PAGE_WRITABLE | PAGE_USER, &image_first_phys)) {
        paging_destroy_address_space(as);
        t->state = TASK_UNUSED;
        return NULL;
    }
    // Copia os bytes do arquivo para a memória física da imagem,
    // usando o endereço FÍSICO (identity-mapped) — não HEXEC_LOAD_ADDR
    // diretamente, que só é válido depois que `as` estiver ativo em
    // CR3 (ver comentário em map_fresh_zeroed_pages_in). Isto assume
    // que hdr.code_size cabe na primeira página física contígua
    // alocada sequencialmente — válido aqui porque pfa_alloc_frame()
    // é chamado em sequência dentro de map_fresh_zeroed_pages_in para
    // páginas cujo endereço VIRTUAL é contíguo, mas as físicas podem
    // não ser. Por isso copiamos byte a byte respeitando os limites de
    // cada página física individualmente, em vez de assumir
    // contiguidade física.
    {
        uint64_t remaining = hdr.code_size;
        uint64_t src_off = 0;
        for (uint64_t p = 0; p < image_pages && remaining > 0; p++) {
            uint64_t va = HEXEC_LOAD_ADDR + p * PAGE_SIZE_4K;
            uint64_t phys;
            if (!paging_translate_in(as, va, &phys)) break; // não deveria falhar
            uint64_t chunk = remaining < PAGE_SIZE_4K ? remaining : PAGE_SIZE_4K;
            kmemcpy((void*)phys,
                    executable_file->data + sizeof(hexec_header_t) + src_off,
                    chunk);
            remaining -= chunk;
            src_off   += chunk;
        }
    }

    // ---- mapeia a pilha de USUÁRIO, dentro do espaço do processo ----
    uint64_t user_stack_base = USER_STACK_VREGION_BASE;
    if (!map_fresh_zeroed_pages_in(as, user_stack_base, PROCESS_USER_STACK_PAGES,
                                    PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXEC, NULL)) {
        task_free_mapped_pages(as, HEXEC_LOAD_ADDR, image_pages);
        paging_destroy_address_space(as);
        t->state = TASK_UNUSED;
        return NULL;
    }
    uint64_t user_stack_top = user_stack_base + PROCESS_USER_STACK_PAGES * PAGE_SIZE_4K;

    // ---- aloca a pilha de KERNEL desta task ----
    // A pilha de KERNEL continua vivendo no espaço de endereço do
    // KERNEL (compartilhado por todos), não no espaço isolado do
    // processo — é para isso que serve RSP0/TSS: mesmo com o
    // processo tendo seu próprio CR3, o código de kernel que atende
    // uma interrupção/syscall vinda dele continua precisando
    // enxergar o kernel inteiro, o que só a metade alta (comum a
    // todos os PML4) garante. task_alloc_kernel_stack_for() já
    // opera sempre no espaço ativo no momento da chamada (aqui,
    // ainda o do kernel/task 0 — process_create nunca troca CR3).
    uint64_t kstack_top, kstack_base, kguard_addr;
    if (!task_alloc_kernel_stack_for(t, &kstack_top, &kstack_base, &kguard_addr)) {
        task_free_mapped_pages(as, user_stack_base, PROCESS_USER_STACK_PAGES);
        task_free_mapped_pages(as, HEXEC_LOAD_ADDR, image_pages);
        paging_destroy_address_space(as);
        t->state = TASK_UNUSED;
        return NULL;
    }

    // ---- monta o contexto inicial: a primeira "troca" para esta
    // task deve terminar em user_bootstrap_trampoline, que por sua
    // vez chama enter_usermode(t->user_entry, t->user_stack_top) ----
    uint64_t* sp = (uint64_t*)kstack_top;
    *(--sp) = (uint64_t)user_bootstrap_trampoline;
    *(--sp) = 0; // RBP
    *(--sp) = 0; // RBX
    *(--sp) = 0; // R12
    *(--sp) = 0; // R13
    *(--sp) = 0; // R14
    *(--sp) = 0; // R15

    t->rsp               = (uint64_t)sp;
    t->stack_base        = kstack_base;
    t->guard_page_addr   = kguard_addr;
    t->is_user_task      = true;
    t->user_entry        = HEXEC_LOAD_ADDR + hdr.entry_offset;
    t->user_stack_top    = user_stack_top;
    t->user_image_base   = HEXEC_LOAD_ADDR;
    t->user_image_pages  = image_pages;
    t->user_stack_base   = user_stack_base;
    t->user_stack_pages  = PROCESS_USER_STACK_PAGES;
    t->address_space     = as;

    task_publish_ready(t, name);
    return t;
}

// ---- Ponte para Ring 3 na primeira execução de um processo ----
//
// Chamada por user_bootstrap_trampoline (ASM) na primeira vez que
// esta task é escalonada. Não recebe argumento por registrador —
// lê tudo de task_current(), que o scheduler já terá atualizado
// via task_set_current() antes de "retornar" para dentro do
// trampolim (mesmo mecanismo de task_switch usado pelas kernel
// threads comuns). Nunca retorna: enter_usermode() faz um IRETQ
// para Ring 3 e a única forma de a CPU voltar para Ring 0 depois
// disso é via interrupção/syscall, nunca um RET normal.
_Noreturn void process_bootstrap(void) {
    task_t* t = task_current();

    // TSS.RSP0 e CR3 já foram atualizados pelo scheduler
    // (do_switch_to, em scheduler.c) antes desta task assumir a
    // CPU — não precisa repetir aqui.
    enter_usermode(t->user_entry, t->user_stack_top);
}
