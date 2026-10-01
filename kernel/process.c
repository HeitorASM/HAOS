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

// Endereço virtual dedicado às pilhas de USUÁRIO — calculado
// dinamicamente a partir do fim da imagem carregada (ver
// process_create), não mais um valor fixo. Antes da v2 do HEXEC
// (seções separadas), a imagem inteira era sempre pequena o
// suficiente para caber num único bloco bem abaixo deste valor —
// mas com .text/.rodata/.data agora podendo somar mais que isso (um
// programa maior, ou simplesmente várias seções cada uma alinhada a
// página), um valor fixo arriscava a pilha COLIDIR com o fim da
// imagem. Calcular a partir de `image_end` elimina essa classe de
// bug por construção: não importa quão grande a imagem seja, a
// pilha sempre começa depois dela.
//
// A folga de 1 página entre o fim da imagem e o início da pilha
// (ver seu uso em process_create) não é uma guard page de verdade
// (nada a torna não-mapeável explicitamente) — é só um respiro para
// tornar mais fácil, ao ler um dump de memória durante debug,
// distinguir visualmente onde a imagem termina e a pilha começa.
// Uma guard page real entre as duas é uma melhoria futura razoável
// se overflow de pilha de USUÁRIO (não de kernel, que já tem guard
// page — ver task.c) se tornar um problema prático a diagnosticar.
#define USER_STACK_GAP_PAGES  1

// Lê os primeiros bytes de um arquivo já carregado em memória pelo
// VFS e detecta a VERSÃO do header HEXEC (v1 ou v2), preenchendo uma
// estrutura normalizada `hexec_info_t` que o resto do loader usa —
// isso mantém process_create() independente de qual versão de header
// o arquivo realmente tem, sem precisar de dois caminhos de código
// inteiros duplicados.
//
// Para v1 (formato antigo, um único bloco RW+USER): text_size é
// tratado como o code_size inteiro, e rodata_size/data_size ficam
// zerados — ou seja, um HEXEC v1 continua sendo mapeado como um
// único bloco, exatamente como sempre foi, só que agora essa
// decisão é explícita em vez de ser "o único jeito que existe".
//
// Nunca confiamos cegamente em bytes vindos de um arquivo (mesmo
// sendo "nosso" formato, um arquivo corrompido ou truncado não pode
// virar um crash ou um mapeamento além do que o arquivo realmente
// contém).
typedef struct {
    uint32_t version;
    uint64_t entry_offset;
    uint64_t text_size;     // v1: code_size inteiro. v2: só .text.
    uint64_t rodata_size;   // v1: sempre 0.
    uint64_t data_size;     // v1: sempre 0.
    uint64_t bss_size;
    uint64_t header_size;   // quantos bytes do arquivo o header
                              // ocupou de fato (v1: 32; v2: 48) —
                              // necessário para saber onde o
                              // conteúdo das seções começa.
} hexec_info_t;

static bool parse_hexec_header(VfsNode* file, hexec_info_t* out) {
    if (!file || file->type != VFS_FILE) return false;
    // Precisa caber pelo menos o menor header possível (v1) para
    // sequer ler o campo `version` com segurança.
    if (file->size < sizeof(hexec_header_v1_t)) return false;

    uint32_t magic;
    kmemcpy(&magic, file->data, sizeof(magic));
    if (magic != HEXEC_MAGIC) return false;

    uint32_t version;
    kmemcpy(&version, file->data + sizeof(uint32_t), sizeof(version));

    if (version == HEXEC_VERSION_1) {
        hexec_header_v1_t hdr;
        kmemcpy(&hdr, file->data, sizeof(hdr));

        uint64_t available = file->size - sizeof(hdr);
        if (hdr.code_size > available) return false;
        // bss_size vem do arquivo (não confiável): limitamos ANTES de
        // qualquer soma para não haver overflow de uint64 nem pedido
        // de memória absurdo (ver HEXEC_MAX_BSS_SIZE em hexec.h).
        if (hdr.bss_size > HEXEC_MAX_BSS_SIZE) return false;
        // O ponto de entrada precisa cair dentro do CÓDIGO presente no
        // arquivo, não no bss zerado depois dele.
        if (hdr.code_size == 0 || hdr.entry_offset >= hdr.code_size) return false;

        out->version       = HEXEC_VERSION_1;
        out->entry_offset  = hdr.entry_offset;
        out->text_size     = hdr.code_size;
        out->rodata_size   = 0;
        out->data_size     = 0;
        out->bss_size      = hdr.bss_size;
        out->header_size   = sizeof(hdr);
        return true;
    }

    if (version == HEXEC_VERSION_2) {
        if (file->size < sizeof(hexec_header_v2_t)) return false;
        hexec_header_v2_t hdr;
        kmemcpy(&hdr, file->data, sizeof(hdr));

        // Cada seção precisa estar alinhada a página no ARQUIVO — é
        // assim que o linker script (libc/hexec.ld) as produz, e é
        // isso que permite ao loader mapear cada uma com flags
        // diferentes sem misturar permissões numa mesma página.
        if ((hdr.text_size   % PAGE_SIZE_4K) != 0) return false;
        if ((hdr.rodata_size % PAGE_SIZE_4K) != 0) return false;
        if ((hdr.data_size   % PAGE_SIZE_4K) != 0) return false;

        // Os três tamanhos vêm do arquivo (não confiável). Somá-los de
        // uma vez (text + rodata + data) podia dar a volta em uint64 e
        // passar na checagem de `available` com um valor pequeno,
        // enquanto o loader depois mapearia páginas pelos tamanhos
        // ORIGINAIS enormes. Subtraímos de `available` passo a passo,
        // então nenhuma operação aqui consegue estourar.
        uint64_t available = file->size - sizeof(hdr);
        if (hdr.text_size > available) return false;
        uint64_t remaining = available - hdr.text_size;
        if (hdr.rodata_size > remaining) return false;
        remaining -= hdr.rodata_size;
        if (hdr.data_size > remaining) return false;

        // .bss não ocupa espaço no arquivo, então não é limitado por
        // `available` — precisa de teto próprio (ver hexec.h).
        if (hdr.bss_size > HEXEC_MAX_BSS_SIZE) return false;

        // entry_offset precisa cair dentro de .text — não faz
        // sentido (e seria um sinal de arquivo malformado ou
        // malicioso) o ponto de entrada apontar para .rodata/.data/.bss.
        // (Também cobre text_size == 0: nenhum entry_offset é < 0.)
        if (hdr.entry_offset >= hdr.text_size) return false;

        out->version       = HEXEC_VERSION_2;
        out->entry_offset  = hdr.entry_offset;
        out->text_size     = hdr.text_size;
        out->rodata_size   = hdr.rodata_size;
        out->data_size     = hdr.data_size;
        out->bss_size      = hdr.bss_size;
        out->header_size   = sizeof(hdr);
        return true;
    }

    return false; // versão desconhecida
}

// Mapeia `pages` páginas físicas novas a partir de `virt_start`,
// DENTRO do espaço de endereço `as` (que pode não estar ativo no
// momento — é exatamente o caso normal ao montar um processo novo).
// Se `src_data` for não-nulo, copia `src_len` bytes dele para o
// início da região (o resto, até completar `pages` páginas
// inteiras, fica zerado); se `src_data` for NULL, a região inteira
// é zerada (uso: pilha de usuário, .bss). Em caso de falha no meio,
// desfaz tudo que já mapeou e retorna false — mesma disciplina de
// "nunca vazar página física parcial" usada em task.c.
//
// A cópia e o zero-fill usam o endereço FÍSICO de cada página (que
// é sempre acessível via identity-map, phys==virt, independente de
// qual espaço de endereço está ativo agora) em vez do endereço
// virtual de destino no processo — porque esse endereço virtual só
// existe no PML4 de `as`, que ainda não está carregado em CR3 neste
// momento. Escrever diretamente em (void*)virt_addr aqui causaria
// um #PF imediato.
bool map_region_in(address_space_t* as, uint64_t virt_start,
                    uint64_t pages, uint64_t flags,
                    const uint8_t* src_data, uint64_t src_len) {
    uint64_t mapped = 0;
    uint64_t copied = 0;
    for (; mapped < pages; mapped++) {
        uint64_t phys = pfa_alloc_frame();
        if (phys == 0) break;

        uint64_t va = virt_start + mapped * PAGE_SIZE_4K;
        if (!paging_map_in(as, va, phys, flags)) {
            pfa_free_frame(phys);
            break;
        }

        uint64_t remaining_src = (src_data && copied < src_len) ? (src_len - copied) : 0;
        uint64_t chunk = remaining_src < PAGE_SIZE_4K ? remaining_src : PAGE_SIZE_4K;
        if (chunk > 0) {
            kmemcpy((void*)phys, src_data + copied, chunk);
            copied += chunk;
        }
        if (chunk < PAGE_SIZE_4K) {
            // Zera o resto da página (ou a página inteira, se chunk==0)
            // — nunca expõe lixo de uso físico anterior a um processo
            // de usuário, seja em .bss puro ou no "resto" de uma
            // seção cujo tamanho não é múltiplo exato de PAGE_SIZE_4K.
            kmemset((uint8_t*)phys + chunk, 0, PAGE_SIZE_4K - chunk);
        }
    }
    if (mapped < pages) {
        task_free_mapped_pages(as, virt_start, mapped);
        return false;
    }
    return true;
}

uint64_t pages_needed_for(uint64_t bytes) {
    return (bytes + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
}

task_t* process_create(const char* name, VfsNode* executable_file,
                        const char* argv_str, uint64_t argv_len) {
    hexec_info_t info;
    if (!parse_hexec_header(executable_file, &info)) return NULL;

    // ---- reserva o slot de task ANTES de mexer em qualquer página,
    // para que uma tabela de tasks cheia falhe rápido sem ter
    // mapeado nada ainda ----
    task_t* t = task_reserve_slot();
    if (!t) return NULL;

    // ---- cria o espaço de endereço isolado deste processo (Fase 3) ----
    address_space_t* as = paging_create_address_space();
    if (!as) {
        t->state = TASK_UNUSED;
        return NULL;
    }

    // ---- layout de endereço virtual dentro do espaço do processo ----
    // Cada seção começa numa página nova, em ordem: .text, .rodata,
    // .data+.bss (contíguos — .bss é só a continuação zero-fill de
    // .data, ambos RW+NX, então não precisam de mapeamentos
    // separados). Para HEXEC v1 (formato antigo), rodata_size e
    // data_size são sempre 0 — nesse caso as seções seguintes
    // simplesmente têm 0 páginas e o layout degenera de volta a "um
    // bloco só" (.text carrega tudo: código+dados originais), que é
    // exatamente o comportamento de sempre para arquivos v1.
    uint64_t text_pages   = pages_needed_for(info.text_size);
    uint64_t rodata_pages = pages_needed_for(info.rodata_size);
    uint64_t data_pages   = pages_needed_for(info.data_size + info.bss_size);
    if (text_pages == 0) text_pages = 1; // um executável sem nenhum
                                           // byte de código não faz
                                           // sentido, mas evita
                                           // mapear 0 páginas mais abaixo.

    uint64_t text_base   = HEXEC_LOAD_ADDR;
    uint64_t rodata_base = text_base   + text_pages   * PAGE_SIZE_4K;
    uint64_t data_base   = rodata_base + rodata_pages * PAGE_SIZE_4K;
    uint64_t image_end   = data_base   + data_pages   * PAGE_SIZE_4K;
    uint64_t image_total_pages = text_pages + rodata_pages + data_pages;

    const uint8_t* file_bytes = (const uint8_t*)executable_file->data + info.header_size;
    uint64_t file_offset = 0; // avança conforme cada seção é consumida do arquivo

    // ---- .text: RX (executável, NUNCA gravável — W^X real) ----
    if (!map_region_in(as, text_base, text_pages, PAGE_USER,
                        file_bytes + file_offset, info.text_size)) {
        paging_destroy_address_space(as);
        t->state = TASK_UNUSED;
        return NULL;
    }
    file_offset += info.text_size;

    // ---- .rodata: RO (nem gravável, nem executável) ----
    if (rodata_pages > 0) {
        if (!map_region_in(as, rodata_base, rodata_pages, PAGE_USER | PAGE_NO_EXEC,
                            file_bytes + file_offset, info.rodata_size)) {
            task_free_mapped_pages(as, text_base, text_pages);
            paging_destroy_address_space(as);
            t->state = TASK_UNUSED;
            return NULL;
        }
        file_offset += info.rodata_size;
    }

    // ---- .data + .bss: RW, NX (gravável, nunca executável) ----
    // .bss não tem bytes no arquivo — map_region_in já zera
    // automaticamente qualquer parte de uma página além do que foi
    // copiado, e páginas inteiras além do fim de info.data_size
    // também ficam zeradas (src_len cobre só .data, não .bss).
    if (data_pages > 0) {
        if (!map_region_in(as, data_base, data_pages, PAGE_USER | PAGE_WRITABLE | PAGE_NO_EXEC,
                            file_bytes + file_offset, info.data_size)) {
            if (rodata_pages > 0) task_free_mapped_pages(as, rodata_base, rodata_pages);
            task_free_mapped_pages(as, text_base, text_pages);
            paging_destroy_address_space(as);
            t->state = TASK_UNUSED;
            return NULL;
        }
    }

    // ---- mapeia a pilha de USUÁRIO, dentro do espaço do processo ----
    // Começa logo após o fim da imagem (ver USER_STACK_GAP_PAGES) —
    // calculado dinamicamente, não mais um endereço fixo, para nunca
    // colidir com uma imagem grande (múltiplas seções v2, cada uma
    // alinhada a página, podem somar mais que os ~256KB de folga que
    // um valor fixo antigo assumia implicitamente).
    uint64_t user_stack_base = image_end + USER_STACK_GAP_PAGES * PAGE_SIZE_4K;

    // Verificação de sanidade: a pilha ainda precisa cair na MESMA
    // entrada de PDPT que HEXEC_LOAD_ADDR (ver comentário extenso em
    // paging_create_address_space, em paging.cpp, sobre por que só
    // essa entrada é exclusiva/isolada por processo). Isso só
    // deixaria de ser verdade com um executável absurdamente grande
    // (a entrada de PDPT cobre 1GB inteiro) — mas falhar aqui com um
    // erro claro é muito melhor do que silenciosamente mapear a
    // pilha numa região compartilhada com o kernel.
    {
        uint64_t hexec_pdpt_idx = (HEXEC_LOAD_ADDR >> 30) & 0x1FF;
        uint64_t stack_pdpt_idx = (user_stack_base >> 30) & 0x1FF;
        if (stack_pdpt_idx != hexec_pdpt_idx) {
            if (data_pages > 0)   task_free_mapped_pages(as, data_base, data_pages);
            if (rodata_pages > 0) task_free_mapped_pages(as, rodata_base, rodata_pages);
            task_free_mapped_pages(as, text_base, text_pages);
            paging_destroy_address_space(as);
            t->state = TASK_UNUSED;
            return NULL;
        }
    }

    if (!map_region_in(as, user_stack_base, PROCESS_USER_STACK_PAGES,
                        PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXEC, NULL, 0)) {
        if (data_pages > 0)   task_free_mapped_pages(as, data_base, data_pages);
        if (rodata_pages > 0) task_free_mapped_pages(as, rodata_base, rodata_pages);
        task_free_mapped_pages(as, text_base, text_pages);
        paging_destroy_address_space(as);
        t->state = TASK_UNUSED;
        return NULL;
    }
    uint64_t user_stack_end = user_stack_base + PROCESS_USER_STACK_PAGES * PAGE_SIZE_4K;
    uint64_t user_stack_top = user_stack_end; // RSP inicial — pode ser
                                                // reduzido mais abaixo
                                                // para acomodar
                                                // argc/argv escritos
                                                // no topo da pilha;
                                                // user_stack_end
                                                // continua fixo,
                                                // marcando o fim REAL
                                                // da região de pilha
                                                // mapeada (usado para
                                                // calcular onde a heap
                                                // começa, depois).

    // ---- monta argc/argv no TOPO da pilha de usuário ----
    //
    // Layout (do endereço mais alto para o mais baixo, como convém a
    // uma pilha que cresce para baixo):
    //   [strings de argv, uma após a outra, cada uma terminada em '\0']
    //   [padding de alinhamento, se necessário]
    //   [argv[argc] = NULL]           <- terminador do array argv
    //   [argv[argc-1] .. argv[0]]     <- ponteiros para as strings acima
    //   [argc]                        <- RSP inicial aponta EXATAMENTE aqui
    //
    // crt0.asm lê argc do topo da pilha (via pop ou [rsp]) e argv
    // como rsp+8, exatamente como o Linux x86_64 entrega esses
    // valores para _start — reaproveitar essa convenção já conhecida
    // evita inventar algo novo sem necessidade.
    //
    // Escrevemos usando o endereço FÍSICO da última página da pilha
    // (via paging_translate_in), não o endereço virtual — pelo mesmo
    // motivo de sempre: o CR3 deste processo ainda não está ativo
    // neste momento (ver map_region_in).
    int argc = 0;
    uint64_t argv_ptrs[32]; // até 32 argumentos — mais que suficiente
                             // para um programa simples desta fase;
                             // aumentar se algum dia for necessário.
    {
        uint64_t last_page_va = user_stack_top - PAGE_SIZE_4K;
        uint64_t last_page_phys;
        if (!paging_translate_in(as, last_page_va, &last_page_phys)) {
            // Não deveria falhar (acabamos de mapear esta página),
            // mas se falhar, seguimos sem argumentos em vez de travar
            // a criação do processo por uma funcionalidade opcional.
            argc = 0;
        } else {
            // write_cursor anda de trás para frente dentro da última
            // página física, copiando as strings de argv_str.
            uint64_t page_end_phys = last_page_phys + PAGE_SIZE_4K;
            uint64_t write_cursor = page_end_phys;

            if (argv_str && argv_len > 0) {
                // Copia argv_str INTEIRO (com seus '\0' internos) para
                // perto do fim da página, de uma vez, então localiza
                // cada substring dentro dessa cópia para os ponteiros
                // de argv[]. Reserva uma margem de segurança (256
                // bytes) no topo da página para o frame argc/argv em
                // si, que é escrito DEPOIS das strings.
                uint64_t max_strings_len = PAGE_SIZE_4K > 256 ? PAGE_SIZE_4K - 256 : 0;
                uint64_t copy_len = argv_len < max_strings_len ? argv_len : max_strings_len;

                write_cursor -= copy_len;
                write_cursor &= ~0xFULL; // alinhamento de 16 bytes,
                                           // por higiene (ABI x86_64
                                           // recomenda pilha alinhada)
                kmemcpy((void*)write_cursor, argv_str, copy_len);

                // Localiza cada string dentro da cópia, convertendo
                // os offsets físicos para os endereços VIRTUAIS
                // correspondentes (é isso que o processo vai
                // realmente enxergar em argv[i] quando rodar).
                uint64_t strings_phys_base = write_cursor;
                uint64_t strings_virt_base = last_page_va + (write_cursor - last_page_phys);

                uint64_t pos = 0;
                while (pos < copy_len && (uint64_t)argc < (sizeof(argv_ptrs) / sizeof(argv_ptrs[0]))) {
                    const char* s = (const char*)(strings_phys_base + pos);
                    uint64_t len = kstrlen(s);
                    if (len == 0 && pos + 1 >= copy_len) break; // string
                                                                   // vazia
                                                                   // no
                                                                   // final
                                                                   // = fim
                                                                   // da
                                                                   // lista
                    argv_ptrs[argc++] = strings_virt_base + pos;
                    pos += len + 1; // pula esta string + seu '\0'
                }
            }

            // ---- monta o array de ponteiros argv[] + terminador NULL ----
            write_cursor -= sizeof(uint64_t); // argv[argc] = NULL
            *(uint64_t*)write_cursor = 0;

            for (int i = argc - 1; i >= 0; i--) {
                write_cursor -= sizeof(uint64_t);
                *(uint64_t*)write_cursor = argv_ptrs[i];
            }
            uint64_t argv_array_virt = last_page_va + (write_cursor - last_page_phys);

            // ---- argc, no topo (RSP inicial vai apontar bem aqui) ----
            write_cursor -= sizeof(uint64_t);
            *(uint64_t*)write_cursor = (uint64_t)argc;

            (void)argv_array_virt; // implícito: argv fica logo após
                                     // argc na pilha, então crt0 só
                                     // precisa fazer rsp+8 para
                                     // alcançá-lo — não precisamos
                                     // passar esse endereço
                                     // separadamente.

            uint64_t new_top_virt = last_page_va + (write_cursor - last_page_phys);
            user_stack_top = new_top_virt;
        }
    }

    // ---- reserva o ENDEREÇO da heap de usuário (sbrk) ----
    // Não mapeia nenhuma página física ainda — sys_sbrk() faz isso
    // sob demanda, conforme o processo pede para crescer (ver
    // syscall.c). Aqui só decidimos ONDE a heap começa e até onde
    // pode crescer no máximo.
    //
    // BUG CRÍTICO CORRIGIDO AQUI: o cálculo usava `user_stack_top`,
    // mas essa variável já tinha sido REDUZIDA logo acima (bloco de
    // montagem de argc/argv), para virar o RSP inicial do processo —
    // um valor MENOR que o topo real da região de pilha mapeada.
    // Calcular heap_base a partir dele fazia a heap começar DENTRO
    // da própria pilha de usuário já mapeada, em vez de logo depois
    // — a primeira chamada a sbrk() (disparada por malloc(), inclusive
    // internamente por printf()/vprintf()) tentava mapear páginas que
    // JÁ estavam mapeadas como parte da pilha, corrompendo o estado
    // de paginação do processo de forma silenciosa e catastrófica —
    // exatamente o tipo de dano que trava o sistema inteiro sem
    // nenhuma exceção de CPU detectável.
    //
    // A correção: usar `user_stack_end`, que permanece FIXO no fim
    // real da região de PROCESS_USER_STACK_PAGES páginas mapeadas,
    // nunca mutado pela montagem de argc/argv (só user_stack_top é
    // mutado, e é usado exclusivamente como RSP inicial).
    //
    // Fica logo após a pilha de usuário, com a mesma folga de
    // USER_STACK_GAP_PAGES usada entre a imagem e a pilha — e pela
    // mesma razão: só facilita distinguir visualmente as regiões
    // num dump de memória durante debug, não é uma proteção real.
    uint64_t heap_base = user_stack_end + USER_STACK_GAP_PAGES * PAGE_SIZE_4K;

    // HEAP_MAX_PAGES limita quanto um processo pode crescer via
    // sbrk — evita que um processo (por bug ou de propósito) consuma
    // toda a memória física disponível do sistema sozinho. 8192
    // páginas = 32MB, generoso para um programa simples desta fase
    // sem abrir mão de um teto sensato; ajustar conforme necessário
    // quando programas maiores existirem.
    #define HEAP_MAX_PAGES  8192ULL
    uint64_t heap_limit = heap_base + HEAP_MAX_PAGES * PAGE_SIZE_4K;

    // Mesma verificação de sanidade da pilha: heap_base precisa cair
    // na MESMA entrada de PDPT que HEXEC_LOAD_ADDR (a única exclusiva
    // por processo — ver paging_create_address_space). Isso só
    // falharia com uma pilha de usuário/imagem absurdamente grandes;
    // falhar aqui com um erro claro é preferível a mapear a heap numa
    // região compartilhada com o kernel silenciosamente.
    {
        uint64_t hexec_pdpt_idx = (HEXEC_LOAD_ADDR >> 30) & 0x1FF;
        uint64_t heap_pdpt_idx  = (heap_base >> 30) & 0x1FF;
        if (heap_pdpt_idx != hexec_pdpt_idx) {
            task_free_mapped_pages(as, user_stack_base, PROCESS_USER_STACK_PAGES);
            if (data_pages > 0)   task_free_mapped_pages(as, data_base, data_pages);
            if (rodata_pages > 0) task_free_mapped_pages(as, rodata_base, rodata_pages);
            task_free_mapped_pages(as, text_base, text_pages);
            paging_destroy_address_space(as);
            t->state = TASK_UNUSED;
            return NULL;
        }
    }

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
        if (data_pages > 0)   task_free_mapped_pages(as, data_base, data_pages);
        if (rodata_pages > 0) task_free_mapped_pages(as, rodata_base, rodata_pages);
        task_free_mapped_pages(as, text_base, text_pages);
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
    t->user_entry        = text_base + info.entry_offset;
    t->user_stack_top    = user_stack_top;
    // user_image_base/user_image_pages cobrem a REGIÃO INTEIRA da
    // imagem (text+rodata+data/bss, contíguas por construção — ver
    // o layout acima), não só .text: o reaper (task_reap_zombies)
    // usa esses dois campos para liberar tudo de uma vez com uma
    // única chamada a task_free_mapped_pages, sem precisar conhecer
    // internamente que a imagem tem múltiplas sub-regiões com
    // permissões diferentes (isso já foi resolvido na hora de
    // MAPEAR; para LIBERAR, todas as páginas entre text_base e
    // image_end pertencem exclusivamente a este processo do mesmo
    // jeito, independente da permissão que tinham).
    t->user_image_base   = text_base;
    t->user_image_pages  = image_total_pages;
    t->user_stack_base   = user_stack_base;
    t->user_stack_pages  = PROCESS_USER_STACK_PAGES;
    t->heap_base         = heap_base;
    t->heap_current      = heap_base; // nada mapeado ainda — sys_sbrk
                                        // avança isso conforme o
                                        // processo pede para crescer.
    t->heap_limit        = heap_limit;
    for (int fd = 0; fd < TASK_MAX_OPEN_FILES; fd++) {
        t->fd_table[fd]  = NULL;
        t->fd_offset[fd] = 0;
    }
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
