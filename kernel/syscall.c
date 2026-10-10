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

#include "random.h"
#include "syscall.h"
#include "gdt.h"
#include "types.h"
#include "memory.h"
#include "paging.h"
#include "task.h"
#include "process.h"
#include "scheduler.h"
#include "../fs/vfs.h"
#include "../fs/perm.h"
#include "user.h"
#include "../gui/core/userwin.h"

extern void syscall_entry(void); // definido em syscall_asm.asm
extern volatile uint64_t timer_ticks; // idt_asm.asm — incrementado a 100 Hz (IRQ0)
#define SYS_MS_PER_TICK 10            // 1000 ms / 100 Hz

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
//
// CORREÇÃO (overflow): a versão anterior calculava
// `end_page = (addr + len - 1) & ~0xFFF` sem checar overflow. Com
// addr = 0xFFFFFFFFFFFFFF80 e len = 256, a soma dava a volta e
// end_page virava 0 — menor que start_page — então o laço abaixo
// NUNCA executava e a função devolvia `true` sem validar página
// nenhuma, deixando sys_write ler memória de kernel. Agora rejeitamos
// (a) qualquer endereço fora da metade de usuário e (b) qualquer
// intervalo que ultrapasse o topo dessa metade, ANTES de somar.
#define USER_SPACE_TOP 0x0000800000000000ULL

static bool validate_user_range_ex(uint64_t addr, uint64_t len, bool need_write) {
    if (len == 0) return true;
    if (addr >= USER_SPACE_TOP) return false;
    if (len > USER_SPACE_TOP - addr) return false; // addr+len não passa do topo (e não estoura)

    uint64_t start_page = addr & ~(PAGE_SIZE_4K - 1);
    uint64_t end_page    = (addr + len - 1) & ~(PAGE_SIZE_4K - 1);

    for (uint64_t page = start_page; page <= end_page; page += PAGE_SIZE_4K) {
        // paging_is_user_page checa DUAS coisas de uma vez: (a) a
        // página está mapeada, e (b) foi mapeada com PAGE_USER — ou
        // seja, pertence de fato ao espaço de endereço deste
        // processo, não é uma página de KERNEL que por coincidência
        // tem uma tradução válida (ex.: a própria pilha de kernel da
        // task, ou o código do kernel). Sem o item (b), um processo
        // malicioso poderia passar o endereço de alguma estrutura de
        // kernel conhecida para sys_write/sys_read e usar a syscall
        // como uma forma de ler/vazar memória de kernel arbitrária.
        // Para destinos que o KERNEL vai escrever, exigimos também
        // GRAVÁVEL (ver paging_is_user_writable_page).
        bool ok = need_write ? paging_is_user_writable_page(page)
                             : paging_is_user_page(page);
        if (!ok) return false;
    }
    return true;
}

// Buffer de ORIGEM (o kernel só LÊ da memória do usuário).
static bool validate_user_range(uint64_t addr, uint64_t len) {
    return validate_user_range_ex(addr, len, false);
}

// Buffer de DESTINO (o kernel ESCREVE na memória do usuário): precisa
// ser gravável, senão um ponteiro para .text/.rodata causaria #PF em
// Ring 0.
static bool validate_user_range_writable(uint64_t addr, uint64_t len) {
    return validate_user_range_ex(addr, len, true);
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
// NOTA HISTÓRICA — isto já foi um risco real, hoje resolvido: até a
// introdução de SYS_WAITPID, syscall_entry usava uma ÚNICA pilha de
// kernel GLOBAL (syscall_stack, em syscall_asm.asm) compartilhada por
// todas as syscalls de todas as tasks. Isso era seguro enquanto a
// ÚNICA syscall que chamava scheduler_yield()/task_switch() era esta
// (sys_exit, um caminho SEM RETORNO — a task morre e nunca mais usa
// aquela pilha). SYS_WAITPID introduziu uma syscall que bloqueia e
// RETORNA (sys_waitpid chama scheduler_yield() em loop, no meio do
// próprio corpo da syscall, esperando um filho terminar) — com a
// pilha global antiga, isso causava reentrância real: um processo
// PAI bloqueado dentro de sys_waitpid (via scheduler_yield) e um
// processo FILHO fazendo suas próprias syscalls ao mesmo tempo
// usavam a MESMA pilha global, cada um sobrescrevendo o que o outro
// tinha empilhado — travava o sistema (ver examples/launcher).
// Corrigido dando a cada task sua PRÓPRIA pilha de kernel para o
// corpo de qualquer syscall (current_syscall_stack_top, atualizada
// por scheduler.c/do_switch_to a cada troca de contexto — ver
// comentário grande em kernel/syscall_asm.asm para os detalhes).
// ---- SYS_SBRK(increment) ----
// Cresce (ou encolhe, se increment for negativo) a heap de usuário
// do processo atual em `increment` bytes, mapeando/desmapeando
// páginas físicas conforme necessário. Retorna o endereço do TOPO
// ANTERIOR da heap (convenção clássica de sbrk() Unix — o chamador
// usa esse valor como o início do novo bloco), ou (uint64_t)-1 em
// erro (heap_limit excedido, ou tentativa de encolher além de
// heap_base).
//
// increment é tratado em unidades de PÁGINA INTEIRA (arredondado
// para cima) — a libc (malloc) é quem gerencia sub-alocações dentro
// de uma página; o kernel só sabe mapear/desmapear páginas inteiras.
static uint64_t sys_sbrk(int64_t increment) {
    task_t* t = task_current();
    if (!t->is_user_task || t->heap_base == 0) {
        // Chamado por uma task sem heap configurada (não deveria
        // acontecer para processos de usuário reais, mas nunca
        // confiamos cegamente vindo de Ring 3).
        return (uint64_t)-1;
    }

    uint64_t old_top = t->heap_current;

    if (increment == 0) {
        return old_top; // sys_sbrk(0) é o idioma clássico para "só
                          // me diga onde a heap está agora, sem
                          // crescer nada" — usado por malloc() para
                          // inicializar seu estado na primeira
                          // chamada.
    }

    if (increment > 0) {
        uint64_t grow_bytes = (uint64_t)increment;
        uint64_t new_top = old_top + grow_bytes;
        if (new_top > t->heap_limit || new_top < old_top /* overflow */) {
            return (uint64_t)-1; // cresceria além do limite permitido
        }

        uint64_t pages = pages_needed_for(grow_bytes);
        // map_region_in mapeia a partir de um endereço ALINHADO A
        // PÁGINA — old_top já é sempre alinhado, porque só avançamos
        // heap_current em múltiplos de PAGE_SIZE_4K (ver abaixo,
        // "avança em páginas inteiras").
        if (!map_region_in(t->address_space, old_top, pages,
                            PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXEC, NULL, 0)) {
            return (uint64_t)-1; // sem memória física suficiente
        }
        t->heap_current = old_top + pages * PAGE_SIZE_4K; // avança em
                                                             // páginas
                                                             // inteiras,
                                                             // não só
                                                             // grow_bytes
                                                             // — mantém
                                                             // heap_current
                                                             // sempre
                                                             // alinhado.
        return old_top;
    }

    // increment < 0: encolhe a heap, desmapeando páginas do topo.
    uint64_t shrink_bytes = (uint64_t)(-increment);
    if (shrink_bytes > (old_top - t->heap_base)) {
        return (uint64_t)-1; // encolheria além do início da heap
    }
    uint64_t pages = pages_needed_for(shrink_bytes);
    uint64_t new_top = old_top - pages * PAGE_SIZE_4K;
    task_free_mapped_pages(t->address_space, new_top, pages);
    t->heap_current = new_top;
    return old_top;
}

// ---- Descritores de arquivo: SYS_OPEN / SYS_READ / SYS_CLOSE ----
//
// Implementação mínima e só-leitura nesta fase: um processo pode
// abrir um arquivo existente do VFS e lê-lo sequencialmente, mas não
// pode criar, escrever ou truncar um arquivo via syscall ainda (isso
// continua sendo uma operação exclusiva do kernel/GUI, ex.: o editor
// de texto usando vfs_write diretamente). Adicionar SYS_WRITE_FILE/
// SYS_CREATE fica para quando um programa de usuário realmente
// precisar disso.
#define SYS_OPEN_MAX_PATH  256

// fds 0, 1 e 2 são RESERVADOS (stdin, stdout, stderr — convenção
// POSIX) e nunca devolvidos aqui. Antes desta correção o primeiro
// arquivo aberto recebia fd 0, que sys_read trata como stdin: um
// read() nesse fd bloqueava esperando teclado em vez de ler o
// arquivo (bug encontrado em teste real no QEMU com examples/notes).
// Consequência: cada processo pode ter TASK_MAX_OPEN_FILES - 3
// arquivos abertos ao mesmo tempo (5 hoje).
#define FIRST_FILE_FD 3

static int find_free_fd(task_t* t) {
    for (int fd = FIRST_FILE_FD; fd < TASK_MAX_OPEN_FILES; fd++) {
        if (t->fd_table[fd] == NULL) return fd;
    }
    return -1;
}

// ---- Helpers de caminho (compartilhados por open/stat/mkdir/...) ----

// Diretório de trabalho efetivo do processo: o cwd dele, ou a raiz se
// ele nunca teve um / o diretório foi removido (VfsNode::removed).
static VfsNode* task_cwd(task_t* t) {
    if (t->cwd && !t->cwd->removed) return t->cwd;
    return vfs_root();
}

// Copia um caminho vindo do usuário para um buffer do kernel
// (out precisa ter SYS_OPEN_MAX_PATH bytes) e termina com '\0'.
static bool copy_user_path(uint64_t user_ptr, uint64_t len, char* out) {
    if (len == 0 || len >= SYS_OPEN_MAX_PATH) return false;
    if (!validate_user_range(user_ptr, len)) return false;
    kmemcpy(out, (const void*)user_ptr, len);
    out[len] = '\0';
    return true;
}

// Credenciais do processo que fez a syscall. Sem bypass: processos de
// usuário nunca têm uid 0 (ver kernel/user.h), então TODA syscall de
// arquivo obedece as permissões do VFS — o isolamento entre contas é
// imposto aqui, na fronteira Ring 3 -> kernel.
static Cred task_cred(task_t* t) {
    return cred_for_ids(t->uid, t->gid);
}

// Separa `path` em "diretório pai (já existente)" + "nome novo".
// MODIFICA `path` (corta no último '/'). Rejeita: pai inexistente ou
// não-diretório, nome vazio (caminho terminado em '/'), "." e "..",
// e nomes >= VFS_NAME_MAX (o VFS guarda o nome em um array fixo —
// ver o overflow corrigido em vfs_alloc_node).
static bool resolve_parent(task_t* t, char* path, uint64_t path_len,
                           VfsNode** out_parent, const char** out_name) {
    int last_slash = -1;
    for (uint64_t i = 0; i < path_len; i++) {
        if (path[i] == '/') last_slash = (int)i;
    }

    VfsNode* parent;
    const char* fname;
    if (last_slash < 0) {
        parent = task_cwd(t);              // "arquivo"
        fname  = path;
    } else if (last_slash == 0) {
        parent = vfs_root();               // "/arquivo"
        fname  = path + 1;
    } else {
        path[last_slash] = '\0';           // corta no último '/'
        Cred cred = task_cred(t);          // exige X em cada diretório do caminho
        if (vfs_resolve_as(&cred, task_cwd(t), path, &parent) != PERM_OK) return false;
        fname  = path + last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIR) return false;
    if (fname[0] == '\0') return false;
    if (kstrlen(fname) >= VFS_NAME_MAX) return false;
    if (kstrcmp(fname, ".") == 0 || kstrcmp(fname, "..") == 0) return false;

    *out_parent = parent;
    *out_name   = fname;
    return true;
}

static uint64_t sys_open(uint64_t user_path_ptr, uint64_t path_len, uint64_t flags) {
    if (path_len == 0 || path_len >= SYS_OPEN_MAX_PATH) return (uint64_t)-1;
    if (!validate_user_range(user_path_ptr, path_len)) return (uint64_t)-1;

    char path[SYS_OPEN_MAX_PATH];
    kmemcpy(path, (const void*)user_path_ptr, path_len);
    path[path_len] = '\0';

    task_t* t = task_current();
    int fd = find_free_fd(t);
    if (fd < 0) return (uint64_t)-1; // tabela de FDs deste processo cheia

    // Caminhos relativos são resolvidos a partir do cwd DESTE processo
    // (herdado do terminal/pai; muda com SYS_CHDIR). Caminhos
    // absolutos ("/...") continuam funcionando como antes.
    //
    // vfs_resolve_as exige permissão de busca (X) em cada diretório do
    // caminho: sem ela falha com PERM_EACCES e NÃO caímos no O_CREAT
    // (criar um arquivo "do outro lado" de um diretório proibido seria
    // contornar a proteção).
    Cred cred = task_cred(t);
    VfsNode* node = NULL;
    int rr = vfs_resolve_as(&cred, task_cwd(t), path, &node);
    if (rr == PERM_EACCES) return (uint64_t)-1;
    if (rr != PERM_OK) node = NULL;

    if (!node && (flags & O_CREAT)) {
        // Arquivo não existe e o processo pediu criação: separa o
        // caminho em "diretório pai" + "nome" (ver resolve_parent) —
        // o pai PRECISA já existir e ser um diretório; não criamos
        // diretórios intermediários.
        VfsNode* parent;
        const char* fname;
        if (!resolve_parent(t, path, path_len, &parent, &fname)) return (uint64_t)-1;

        node = vfs_touch_as(&cred, parent, fname, NULL); // exige W+X no pai; dono = este processo
    }

    if (!node || node->type != VFS_FILE) return (uint64_t)-1;

    // Permissão do próprio arquivo: leitura sempre; escrita se O_WRONLY.
    // (Num arquivo recém-criado o dono tem rw-, então passa.)
    if (!perm_allows(node, &cred, PERM_R)) return (uint64_t)-1;
    if ((flags & O_WRONLY) && !perm_allows(node, &cred, PERM_W)) return (uint64_t)-1;

    // O_TRUNC: só tem efeito se também abriu para escrita — truncar
    // num open somente-leitura seria destrutivo sem que o processo
    // tenha declarado intenção de gravar. Sincroniza aqui (diferente
    // de sys_write_fd, que só sincroniza no close — ver comentário
    // lá) porque O_TRUNC é uma operação destrutiva pontual, não algo
    // chamado em loop; deixar sem persistir criaria uma janela onde
    // o disco ainda tem o conteúdo ANTIGO mas a RAM já mostra vazio
    // — se o sistema reiniciasse nesse meio-tempo sem o processo
    // nunca ter chegado a escrever/fechar, o próximo boot ressuscitaria
    // dados que o programa já considerava apagados.
    if ((flags & O_WRONLY) && (flags & O_TRUNC)) {
        node->size = 0;
        if (node->data && node->capacity > 0) node->data[0] = '\0';
        vfs_sync();
    }

    t->fd_table[fd]    = node;
    t->fd_offset[fd]   = (flags & O_WRONLY) ? FD_WRITABLE_BIT : 0;
    return (uint64_t)fd;
}

// ---- SYS_WRITE_FD(fd, buf, count) — escrita em arquivo ----
//
// Escreve `count` bytes de `buf` (memória do processo) no arquivo
// aberto em `fd`, a partir de fd_offset[fd], e avança o offset.
// Cresce o arquivo com vfs_ensure_capacity se necessário. Usa
// kmemcpy direto sobre node->data em vez de vfs_write/vfs_append
// porque estes recebem uma string C terminada em '\0' e cortariam
// dados binários no primeiro byte zero.
//
// Copia em pedaços de no máximo SYS_WRITE_FD_CHUNK bytes por vez,
// pela mesma disciplina de pilha de kernel previsível do sys_read
// (nada de buffer grande na pilha) — aqui nem precisa de buffer
// intermediário, copiamos direto do ponteiro de usuário (já
// validado) para node->data.
static uint64_t sys_write_fd(uint64_t fd, uint64_t user_buf_ptr, uint64_t count) {
    task_t* t = task_current();
    if (fd >= TASK_MAX_OPEN_FILES || t->fd_table[fd] == NULL) return (uint64_t)-1;
    if (!(t->fd_offset[fd] & FD_WRITABLE_BIT)) return (uint64_t)-1; // aberto só para leitura
    if (count == 0) return 0;
    if (!validate_user_range(user_buf_ptr, count)) return (uint64_t)-1;

    VfsNode* node = t->fd_table[fd];
    if (node->removed) return (uint64_t)-1; // arquivo apagado com o fd aberto
    uint64_t offset = t->fd_offset[fd] & FD_OFFSET_MASK;

    // Checa overflow/limite ANTES de alocar: offset+count precisa
    // caber em VFS_FILE_MAX (1 MB) e em uint32_t.
    if (offset > VFS_FILE_MAX || count > VFS_FILE_MAX - offset) return (uint64_t)-1;
    uint32_t end = (uint32_t)(offset + count);

    // +1 para manter um '\0' final após o último byte, já que o
    // resto do sistema (cat, editor) trata node->data como string C.
    if (!vfs_ensure_capacity(node, end + 1)) return (uint64_t)-1;

    // Se o offset ficou além do tamanho atual (não acontece com o
    // fluxo sequencial atual, mas é barato garantir), preenche o
    // buraco com zeros em vez de expor lixo de memória.
    if (offset > node->size) {
        kmemset(node->data + node->size, 0, (size_t)(offset - node->size));
    }

    kmemcpy(node->data + offset, (const void*)user_buf_ptr, (size_t)count);
    if (end > node->size) node->size = end;
    node->data[node->size] = '\0';

    t->fd_offset[fd] = end | FD_WRITABLE_BIT;
    return count;
}

// ---- SYS_READ(fd=0, buf, count) — leitura de stdin (teclado) ----
//
// fd 0 é tratado à parte de fd_table/VFS: não é um arquivo, é o
// stdin_ring de `t` (ver task.h), alimentado byte a byte pelo
// terminal dono (task_stdin_write, chamado do lado da GUI a cada
// tecla) ENQUANTO este processo estiver em foco nele. Diferente de
// ler um arquivo do VFS, aqui BLOQUEIA de verdade se não houver dado
// ainda disponível — um programa que chama read(0, buf, N) espera
// receber o que o usuário digitar, não um "0 bytes lidos" imediato
// como se fosse fim de arquivo.
//
// Mecanismo de bloqueio: marca a task como TASK_BLOCKED +
// waiting_for_stdin=true e chama scheduler_yield() — isso cede a CPU
// para outra task (normalmente a task 0/GUI, que é quem vai
// eventualmente chamar task_stdin_write() em resposta a uma tecla).
// scheduler_tick()/pick_next() já pulam tasks TASK_BLOCKED
// automaticamente (scheduler.c), então esta task simplesmente não
// roda de novo até task_stdin_write() a devolver para TASK_READY.
// Quando scheduler_yield() finalmente RETORNA aqui (a task foi
// escalonada de novo), o laço tenta ler outra vez — se ainda não
// houver nada (não deveria acontecer nesse desenho, mas por defesa,
// ex. spurious wakeup), bloqueia de novo em vez de assumir sucesso.
static uint64_t sys_read_stdin(task_t* t, uint64_t user_buf_ptr, uint64_t count) {
    if (count == 0) return 0;

    #define SYS_READ_STDIN_MAX_LEN 64 // uma tecla de cada vez é o caso
                                        // comum; um chunk pequeno também
                                        // mantém a pilha de kernel desta
                                        // syscall previsível, mesma
                                        // disciplina do SYS_READ_MAX_LEN
                                        // de arquivo, abaixo.
    uint8_t buf[SYS_READ_STDIN_MAX_LEN];
    uint64_t want = count < SYS_READ_STDIN_MAX_LEN ? count : SYS_READ_STDIN_MAX_LEN;

    // Só `want` bytes (no máximo) serão escritos no buffer do usuário
    // — validamos exatamente isso, e exigindo página GRAVÁVEL.
    if (!validate_user_range_writable(user_buf_ptr, want)) return (uint64_t)-1;

    for (;;) {
        uint64_t n = task_stdin_read(t, buf, want);
        if (n > 0) {
            kmemcpy((void*)user_buf_ptr, buf, n);
            return n;
        }

        // Nada disponível ainda: dorme até alguém digitar algo. Se o
        // processo já foi desassociado do terminal (fechou a janela,
        // ver ~TerminalWindow em terminal.cpp) não há mais ninguém
        // para produzir stdin — sem essa checagem, o processo
        // travaria bloqueado para sempre, nunca mais escalonável a
        // não ser por sys_exit chamado de outro lugar (impossível,
        // já que ele nunca mais roda). Nesse caso devolvemos EOF (0),
        // igual a um arquivo sem mais dados.
        if (t->stdout_owner == NULL) return 0;

        t->waiting_for_stdin = true;
        t->state = TASK_BLOCKED;
        scheduler_yield(); // "some" daqui até ser escalonada de novo
    }
}

static uint64_t sys_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t count) {
    task_t* t = task_current();
    if (fd == 0) return sys_read_stdin(t, user_buf_ptr, count);
    if (fd >= TASK_MAX_OPEN_FILES || t->fd_table[fd] == NULL) return (uint64_t)-1;

    // SYS_READ_MAX_LEN era 4096 (uma página inteira) NA PILHA DE
    // KERNEL desta task — que tem só TASK_STACK_SIZE (16KB) no
    // total, guard page incluída. Um buffer tão grande numa única
    // função, somado ao resto da cadeia de chamadas até aqui
    // (syscall_entry -> syscall_dispatch -> sys_read), deixava pouca
    // margem antes de bater na guard page em cenários com mais
    // frames na pilha do que uma chamada isolada de teste sugere.
    // Reduzido para 512 (mesma ordem de grandeza do buffer usado por
    // sys_write) e lendo em BLOCOS quando `count` pede mais que isso
    // — mais chamadas de loop, mas cada uma com pilha previsível.
    #define SYS_READ_MAX_LEN 512

    VfsNode* node = t->fd_table[fd];
    // Arquivo removido (rm) enquanto este fd estava aberto: o nó em
    // si continua alocado (o VFS não libera nós — ver vfs.c), mas o
    // conteúdo já foi liberado. Nunca acessar node->data nesse caso.
    if (node->removed || !node->data) return (uint64_t)-1;

    uint32_t offset = t->fd_offset[fd] & FD_OFFSET_MASK;
    if (offset >= node->size) return 0; // fim de arquivo

    uint32_t available = node->size - offset;
    uint64_t total_to_copy = count < available ? count : available;

    // Valida só o trecho que de fato será escrito, e exige página
    // GRAVÁVEL (um buffer em .text/.rodata causaria #PF no kernel).
    if (!validate_user_range_writable(user_buf_ptr, total_to_copy)) return (uint64_t)-1;

    uint8_t buf[SYS_READ_MAX_LEN];
    uint64_t copied = 0;
    while (copied < total_to_copy) {
        uint64_t chunk = total_to_copy - copied;
        if (chunk > SYS_READ_MAX_LEN) chunk = SYS_READ_MAX_LEN;
        kmemcpy(buf, node->data + offset + copied, chunk);
        kmemcpy((void*)(user_buf_ptr + copied), buf, chunk);
        copied += chunk;
    }
    // Cópia em duas etapas (kernel -> buf -> usuário) em vez de
    // copiar direto node->data -> user_buf_ptr: mantém a mesma
    // disciplina de "nunca escrever direto de uma fonte não validada
    // para um destino de usuário sem um buffer intermediário
    // limitado" usada em sys_write, mesmo aqui a fonte sendo
    // confiável (dados do próprio VFS) — o que precisa de cautela é
    // o TAMANHO copiado de cada vez nunca exceder SYS_READ_MAX_LEN.
    uint64_t to_copy = total_to_copy;

    t->fd_offset[fd] += (uint32_t)to_copy;
    return to_copy;
}

static uint64_t sys_close(uint64_t fd) {
    task_t* t = task_current();
    if (fd >= TASK_MAX_OPEN_FILES || t->fd_table[fd] == NULL) return (uint64_t)-1;

    // Persistência em disco: NÃO chamamos vfs_sync() a cada
    // sys_write_fd (haosfs_save regrava o filesystem INTEIRO do zero
    // a cada chamada — ver comentário grande em fs/haosfs.c — então
    // fazer isso por escrita faria um programa que grava em vários
    // pedaços pequenos, como examples/notes, ficar extremamente lento
    // e desgastar o disco à toa). Em vez disso, sincronizamos aqui,
    // no close() — o ponto natural de "terminei de escrever este
    // arquivo" — e de novo no reaper (task_reap_zombies, kernel/
    // task.c) para não perder dados de um processo que termina sem
    // fechar o fd explicitamente. vfs_sync() é seguro de chamar
    // mesmo sem filesystem persistente montado (device virtual/sem
    // disco): ela mesma verifica isso e não faz nada nesse caso.
    if (t->fd_offset[fd] & FD_WRITABLE_BIT) {
        vfs_sync();
    }

    t->fd_table[fd]  = NULL;
    t->fd_offset[fd] = 0;
    return 0;
}

// ---- SYS_SPAWN — um processo inicia outro ----
//
// Ver o comentário em syscall.h sobre por que isto é spawn e não
// fork+exec. Fluxo: copia path/argv do espaço do pai para buffers do
// kernel (validando os ponteiros), resolve o executável no VFS,
// cria o filho com process_create, e pede ao terminal dono do pai
// (via gancho registrado por terminal.cpp) para adotá-lo. Se a
// adoção falhar (sem terminal, ou sem slot livre), o filho é morto
// na hora — melhor falhar limpo do que deixar um processo órfão
// cuja saída ninguém drena (o stdout_ring dele encheria e a
// saída seria perdida silenciosamente).
#define SYS_SPAWN_MAX_ARGV 256

static uint64_t sys_spawn(uint64_t user_path_ptr, uint64_t path_len,
                          uint64_t user_argv_ptr, uint64_t argv_len) {
    if (path_len == 0 || path_len >= SYS_OPEN_MAX_PATH) return (uint64_t)-1;
    if (!validate_user_range(user_path_ptr, path_len)) return (uint64_t)-1;
    if (argv_len > SYS_SPAWN_MAX_ARGV) return (uint64_t)-1;
    if (argv_len > 0 && !validate_user_range(user_argv_ptr, argv_len)) return (uint64_t)-1;

    task_t* parent = task_current();
    if (parent->stdout_owner == NULL) return (uint64_t)-1; // sem terminal para adotar o filho
    task_adopt_child_fn adopt = task_get_adopt_child_hook();
    if (!adopt) return (uint64_t)-1;

    char path[SYS_OPEN_MAX_PATH];
    kmemcpy(path, (const void*)user_path_ptr, path_len);
    path[path_len] = '\0';

    // Caminho relativo: a partir do cwd do PAI (o filho herda o mesmo cwd).
    // Rodar um programa exige poder LER o arquivo (e atravessar os diretórios
    // do caminho) — o filho herda a identidade do pai, nunca ganha privilégio.
    Cred pcred = task_cred(parent);
    VfsNode* node = NULL;
    if (vfs_resolve_as(&pcred, task_cwd(parent), path, &node) != PERM_OK) return (uint64_t)-1;
    if (!node || node->type != VFS_FILE) return (uint64_t)-1;
    if (!perm_allows(node, &pcred, PERM_R)) return (uint64_t)-1;

    char argv_buf[SYS_SPAWN_MAX_ARGV + SYS_OPEN_MAX_PATH + 2];
    uint64_t alen = 0;
    if (argv_len > 0) {
        kmemcpy(argv_buf, (const void*)user_argv_ptr, argv_len);
        alen = argv_len;
        argv_buf[alen] = '\0'; // garante o último token terminado
        if (argv_buf[alen - 1] != '\0') alen++;
    } else {
        // Sem argv explícito: argv[0] = o próprio caminho.
        kmemcpy(argv_buf, path, path_len + 1);
        alen = path_len + 1;
    }

    // Nome da task = último componente do caminho (o que aparece
    // no prefixo "[nome]" do terminal).
    const char* task_name = path;
    for (uint64_t i = 0; i < path_len; i++) {
        if (path[i] == '/') task_name = path + i + 1;
    }

    task_t* child = process_create_as(task_name, node, argv_buf, alen, task_cwd(parent),
                                      parent->uid, parent->gid);
    if (!child) return (uint64_t)-1;

    child->stdout_owner = parent->stdout_owner;
    if (!adopt(parent->stdout_owner, child)) {
        child->stdout_owner = NULL;
        task_kill(child);
        return (uint64_t)-1;
    }
    return child->id; // nunca 0 (s_next_id começa em 1) — usado por
                       // sys_waitpid(child_id) para localizar esta
                       // task depois, mesmo que o pai já tenha
                       // perdido o task_t* (só guardou o id).
}

static _Noreturn uint64_t sys_exit(uint64_t code) {
    task_t* t = task_current();
    // Marcado ANTES de task_exit(): o terminal dono (stdout_owner)
    // observa has_exited para mostrar "processo encerrado (código X)"
    // uma única vez, mesmo enquanto a task ainda está TASK_ZOMBIE
    // aguardando o reaper — sem isso, o terminal não teria como saber
    // que o processo terminou até o slot inteiro ser reciclado.
    t->exit_code  = code;
    t->has_exited = true;

    // Se algum processo PAI está bloqueado dentro de sys_waitpid(t)
    // esperando exatamente por esta task (ver waiter_pid em task.h),
    // acorda ele agora — do contrário ficaria TASK_BLOCKED para
    // sempre, já que pick_next() nunca escolhe uma task bloqueada por
    // conta própria (ela só volta a TASK_READY quando algo faz isso
    // explicitamente, o mesmo padrão usado por task_stdin_write para
    // acordar um sys_read(stdin) bloqueado).
    if (t->waiter_pid != 0) {
        task_t* waiter = task_find_by_id(t->waiter_pid);
        if (waiter && waiter->state == TASK_BLOCKED) {
            waiter->state = TASK_READY;
        }
    }

    task_exit(); // nunca retorna
    __builtin_unreachable();
}

// ---- SYS_WAITPID(pid) — espera um processo filho terminar ----
//
// Bloqueia (mesmo mecanismo de sys_read_stdin: TASK_BLOCKED +
// scheduler_yield() em loop) até a task de id `pid` ter has_exited,
// e então devolve o exit_code dela — depois disso o reaper fica
// livre para reciclar o slot (ver waiter_pid em task.h e a checagem
// correspondente em task_reap_zombies).
//
// `pid` aqui é sempre o id devolvido por sys_spawn em sucesso (nunca
// um pid "adivinhado" por um processo não relacionado) — não há
// nenhuma checagem de que quem chamou waitpid é de fato o pai daquela
// task; qualquer processo que conheça o id pode esperar por ele. Não
// é uma falha de isolamento séria nesta fase (um processo malicioso
// no máximo consegue "roubar" a espera de outro, não ler memória
// alheia), mas fica registrado como simplificação.
//
// Retorna: 1 em sucesso (com o exit_code do filho escrito em
// *user_out_ptr, se não-NULL e o ponteiro for válido), 0 se `pid` não
// corresponde a NENHUMA task viva (nunca existiu, ou já foi
// totalmente reciclada — só pode acontecer se ninguém mais chamou
// waitpid nela antes e o reaper já limpou), ou (uint64_t)-1 se o
// próprio `pid` for inválido (0) ou já for de uma task ZUMBI
// esperada por OUTRO waiter (waiter_pid já ocupado — esta versão só
// suporta um único waiter por task, não um "waitpid em grupo").
static uint64_t sys_waitpid(uint64_t pid, uint64_t user_out_ptr) {
    if (pid == 0) return (uint64_t)-1;

    task_t* self = task_current();
    task_t* child = task_find_by_id(pid);
    if (!child) return 0; // nunca existiu, ou já foi reciclada sem ninguém esperar

    if (child->waiter_pid != 0 && child->waiter_pid != self->id) {
        return (uint64_t)-1; // outro processo já está esperando por esta task
    }
    child->waiter_pid = self->id; // "reserva" a task para não ser reciclada
                                    // (ver task_reap_zombies) até lermos o
                                    // resultado — idempotente se já era self->id.

    while (!child->has_exited) {
        // Reconfirma a cada volta: task_find_by_id pode não achar
        // mais a task se, por algum caminho que não passe por
        // waiter_pid, ela tiver sido reciclada enquanto dormíamos
        // (não deveria acontecer dado o check em task_reap_zombies,
        // mas checar de novo é barato e evita um ponteiro obsoleto).
        self->state = TASK_BLOCKED;
        scheduler_yield();
        if (task_find_by_id(pid) != child) return (uint64_t)-1; // sumiu — não deveria, mas por segurança
    }

    if (user_out_ptr != 0 && validate_user_range_writable(user_out_ptr, sizeof(uint64_t))) {
        uint64_t code = child->exit_code;
        kmemcpy((void*)user_out_ptr, &code, sizeof(code));
    }

    child->waiter_pid = 0; // libera — reaper agora pode reciclar assim
                             // que stdout_owner também estiver NULL
    return 1;
}

// ============================================================
//  Syscalls 9..19 — identidade, tempo, sistema de arquivos
// ============================================================

static uint64_t sys_getpid(void) {
    return task_current()->id;
}

static uint64_t sys_yield(void) {
    scheduler_yield();
    return 0;
}

static uint64_t sys_uptime_ms(void) {
    return timer_ticks * SYS_MS_PER_TICK;
}

// Dorme cedendo a CPU em laço (mesmo padrão de SYS_WAITPID). As
// syscalls entram com IF=0 (SFMASK), então o tempo só avança porque
// OUTRAS tasks (a GUI, task 0) continuam rodando com interrupções
// ligadas enquanto esta cede a CPU — se algum dia esta for a única
// task escalonável, o laço nunca terminaria. Não é um problema hoje
// (a task 0 sempre existe), mas é a razão de não usarmos `hlt` aqui.
// Um "wake_tick" no scheduler (task BLOCKED até o prazo) evitaria o
// polling — fica como melhoria futura.
static uint64_t sys_sleep_ms(uint64_t ms) {
    if (ms > SYS_SLEEP_MAX_MS) ms = SYS_SLEEP_MAX_MS;
    uint64_t ticks    = (ms + SYS_MS_PER_TICK - 1) / SYS_MS_PER_TICK;
    uint64_t deadline = timer_ticks + ticks;
    while (timer_ticks < deadline) {
        scheduler_yield();
    }
    return 0;
}

static inline uint32_t haos_type_of(const VfsNode* n) {
    return n->type == VFS_DIR ? HAOS_TYPE_DIR : HAOS_TYPE_FILE;
}
static inline uint32_t haos_size_of(const VfsNode* n) {
    return n->type == VFS_DIR ? n->child_count : n->size;
}

static uint64_t sys_stat(uint64_t user_path_ptr, uint64_t path_len, uint64_t user_out_ptr) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;
    if (!validate_user_range_writable(user_out_ptr, sizeof(haos_stat_t))) return (uint64_t)-1;

    Cred cred = task_cred(task_current());
    VfsNode* n = NULL;
    if (vfs_resolve_as(&cred, task_cwd(task_current()), path, &n) != PERM_OK || !n)
        return (uint64_t)-1;

    haos_stat_t st;
    st.type = haos_type_of(n);
    st.size = haos_size_of(n);
    kmemcpy((void*)user_out_ptr, &st, sizeof(st));
    return 0;
}

static uint64_t sys_readdir(uint64_t user_path_ptr, uint64_t path_len,
                            uint64_t index, uint64_t user_out_ptr) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;
    if (!validate_user_range_writable(user_out_ptr, sizeof(haos_dirent_t))) return (uint64_t)-1;

    Cred cred = task_cred(task_current());
    VfsNode* dir = NULL;
    if (vfs_resolve_as(&cred, task_cwd(task_current()), path, &dir) != PERM_OK) return (uint64_t)-1;
    if (!dir || dir->type != VFS_DIR) return (uint64_t)-1;
    if (!perm_allows(dir, &cred, PERM_R)) return (uint64_t)-1;       // listar exige R
    if (index >= dir->child_count) return 0; // fim da listagem

    const VfsNode* c = dir->children[index];
    haos_dirent_t e;
    kmemset(&e, 0, sizeof(e));
    e.type = haos_type_of(c);
    e.size = haos_size_of(c);
    for (uint32_t i = 0; i < HAOS_NAME_MAX - 1 && c->name[i]; i++) e.name[i] = c->name[i];
    kmemcpy((void*)user_out_ptr, &e, sizeof(e));
    return 1;
}

static uint64_t sys_getcwd(uint64_t user_buf_ptr, uint64_t size) {
    char path[SYS_OPEN_MAX_PATH];
    vfs_path_of(task_cwd(task_current()), path, sizeof(path));
    uint64_t len = kstrlen(path);
    if (size < len + 1) return (uint64_t)-1;
    if (!validate_user_range_writable(user_buf_ptr, len + 1)) return (uint64_t)-1;
    kmemcpy((void*)user_buf_ptr, path, len + 1);
    return len;
}

static uint64_t sys_chdir(uint64_t user_path_ptr, uint64_t path_len) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;

    task_t* t = task_current();
    Cred cred = task_cred(t);
    VfsNode* n = NULL;
    if (vfs_resolve_as(&cred, task_cwd(t), path, &n) != PERM_OK) return (uint64_t)-1;
    if (!n || n->type != VFS_DIR) return (uint64_t)-1;
    if (!perm_allows(n, &cred, PERM_X)) return (uint64_t)-1;         // entrar exige X
    t->cwd = n;
    return 0;
}

static uint64_t sys_mkdir(uint64_t user_path_ptr, uint64_t path_len) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;

    VfsNode* parent;
    const char* name;
    if (!resolve_parent(task_current(), path, path_len, &parent, &name)) return (uint64_t)-1;
    Cred cred = task_cred(task_current());
    int err = PERM_OK;
    if (vfs_find_child(parent, name)) return (uint64_t)-1;           // já existe (como antes)
    return vfs_mkdir_as(&cred, parent, name, &err) ? 0 : (uint64_t)-1;
}

static uint64_t sys_unlink(uint64_t user_path_ptr, uint64_t path_len) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;

    Cred cred = task_cred(task_current());
    VfsNode* n = NULL;
    if (vfs_resolve_as(&cred, task_cwd(task_current()), path, &n) != PERM_OK) return (uint64_t)-1;
    if (!n || !n->parent || n == vfs_root()) return (uint64_t)-1;     // inexistente / raiz
    if (n->type == VFS_DIR && n->child_count > 0) return (uint64_t)-1; // só diretório VAZIO
    if (n == vfs_cwd()) return (uint64_t)-1;                           // cwd global do terminal
    // W+X no diretório pai (+ regra sticky de /tmp): ver vfs_remove_as.
    return vfs_remove_as(&cred, n) == PERM_OK ? 0 : (uint64_t)-1;
}

// rename(old, new): move/renomeia. O destino é "pasta-pai + nome novo" (não
// entra em pasta existente como o `mv` do shell) e nunca sobrescreve.
static uint64_t sys_rename(uint64_t old_ptr, uint64_t old_len, uint64_t new_ptr, uint64_t new_len) {
    char oldp[SYS_OPEN_MAX_PATH], newp[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(old_ptr, old_len, oldp)) return (uint64_t)-1;
    if (!copy_user_path(new_ptr, new_len, newp)) return (uint64_t)-1;

    task_t* t = task_current();
    Cred cred = task_cred(t);
    VfsNode* src = NULL;
    if (vfs_resolve_as(&cred, task_cwd(t), oldp, &src) != PERM_OK || !src) return (uint64_t)-1;
    if (src == vfs_root()) return (uint64_t)-1;

    VfsNode* parent;
    const char* name;
    if (!resolve_parent(t, newp, new_len, &parent, &name)) return (uint64_t)-1;
    return vfs_rename_as(&cred, src, parent, name) == PERM_OK ? 0 : (uint64_t)-1;
}

static uint64_t sys_getuid(void) { return task_current()->uid; }
static uint64_t sys_getgid(void) { return task_current()->gid; }

static uint64_t sys_getrandom(uint64_t user_buf_ptr, uint64_t len) {
    if (len == 0 || len > 256) return (uint64_t)-1;
    if (!validate_user_range_writable(user_buf_ptr, len)) return (uint64_t)-1;
    uint8_t tmp[256];
    rng_bytes(tmp, (size_t)len);
    kmemcpy((void*)user_buf_ptr, tmp, (size_t)len);
    kmemset(tmp, 0, sizeof(tmp));
    return len;
}

static uint64_t sys_chmod(uint64_t user_path_ptr, uint64_t path_len, uint64_t mode) {
    char path[SYS_OPEN_MAX_PATH];
    if (!copy_user_path(user_path_ptr, path_len, path)) return (uint64_t)-1;
    if (mode > VFS_MODE_MASK) return (uint64_t)-1;

    task_t* t = task_current();
    Cred cred = task_cred(t);
    VfsNode* n = NULL;
    if (vfs_resolve_as(&cred, task_cwd(t), path, &n) != PERM_OK || !n) return (uint64_t)-1;
    return vfs_chmod_as(&cred, n, (uint16_t)mode) == PERM_OK ? 0 : (uint64_t)-1;
}

static uint64_t sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence) {
    task_t* t = task_current();
    if (fd >= TASK_MAX_OPEN_FILES || t->fd_table[fd] == NULL) return (uint64_t)-1;
    VfsNode* node = t->fd_table[fd];
    if (node->removed) return (uint64_t)-1;

    int64_t cur = (int64_t)(t->fd_offset[fd] & FD_OFFSET_MASK);
    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0;                  break;
        case SEEK_CUR: base = cur;                break;
        case SEEK_END: base = (int64_t)node->size; break;
        default:       return (uint64_t)-1;
    }

    int64_t pos;
    if (__builtin_add_overflow(base, (int64_t)offset, &pos)) return (uint64_t)-1;
    if (pos < 0 || pos > (int64_t)VFS_FILE_MAX) return (uint64_t)-1;

    // Preserva o bit "aberto para escrita" (FD_WRITABLE_BIT).
    t->fd_offset[fd] = (t->fd_offset[fd] & FD_WRITABLE_BIT) | (uint32_t)pos;
    return (uint64_t)pos;
}

// ============================================================
//  Syscalls 20..26 — janelas gráficas (ver gui/core/userwin.cpp)
//
//  Esta camada só valida ponteiros de usuário e copia dados para
//  estruturas do kernel; quem manipula janelas é userwin_*, que não
//  conhece memória de usuário (exceto o `pixels` do blit, já
//  validado aqui). `handle` vindo do usuário é tratado como int64:
//  valores "negativos" (>= 2^63) são recusados por userwin_*.
// ============================================================

#define SYS_WIN_TITLE_MAX     63
#define SYS_WIN_TEXT_MAX      255
#define SYS_WIN_BLIT_MAX_BYTES (16u * 1024u * 1024u)  // 16 MB por blit
#define SYS_WIN_WAIT_FOREVER  0xFFFFFFFFull

static uint64_t sys_win_create(uint64_t w, uint64_t h,
                               uint64_t user_title_ptr, uint64_t title_len) {
    if (w > USERWIN_MAX_DIM || h > USERWIN_MAX_DIM) return (uint64_t)-1;

    char title[SYS_WIN_TITLE_MAX + 1];
    uint64_t n = 0;
    if (user_title_ptr != 0 && title_len > 0) {
        n = title_len > SYS_WIN_TITLE_MAX ? SYS_WIN_TITLE_MAX : title_len;
        if (!validate_user_range(user_title_ptr, n)) return (uint64_t)-1;
        kmemcpy(title, (const void*)user_title_ptr, n);
    }
    title[n] = '\0';   // userwin_create troca título vazio por "App"

    return (uint64_t)userwin_create(task_current()->id, (uint32_t)w, (uint32_t)h, title);
}

static uint64_t sys_win_destroy(uint64_t handle) {
    return userwin_destroy(task_current()->id, (int64_t)handle) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t sys_win_fill(uint64_t handle, uint64_t user_rect_ptr) {
    haos_win_rect_t r;
    if (!validate_user_range(user_rect_ptr, sizeof(r))) return (uint64_t)-1;
    kmemcpy(&r, (const void*)user_rect_ptr, sizeof(r));
    return userwin_fill(task_current()->id, (int64_t)handle, &r) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t sys_win_text(uint64_t handle, uint64_t user_text_ptr) {
    haos_win_text_t t;
    if (!validate_user_range(user_text_ptr, sizeof(t))) return (uint64_t)-1;
    kmemcpy(&t, (const void*)user_text_ptr, sizeof(t));

    if (t.len > SYS_WIN_TEXT_MAX) t.len = SYS_WIN_TEXT_MAX;
    char buf[SYS_WIN_TEXT_MAX + 1];
    if (t.len > 0) {
        if (!validate_user_range(t.text_ptr, t.len)) return (uint64_t)-1;
        kmemcpy(buf, (const void*)t.text_ptr, t.len);
    }
    buf[t.len] = '\0';

    return userwin_text(task_current()->id, (int64_t)handle, t.x, t.y,
                        t.fg, t.bg, t.flags, buf, t.len) == 0 ? 0 : (uint64_t)-1;
}

static uint64_t sys_win_blit(uint64_t handle, uint64_t user_blit_ptr) {
    haos_win_blit_t b;
    if (!validate_user_range(user_blit_ptr, sizeof(b))) return (uint64_t)-1;
    kmemcpy(&b, (const void*)user_blit_ptr, sizeof(b));

    if (b.w <= 0 || b.h <= 0 || b.w > USERWIN_MAX_DIM || b.h > USERWIN_MAX_DIM)
        return (uint64_t)-1;
    uint64_t bytes = (uint64_t)b.w * (uint64_t)b.h * 4u;   // <= 4096*4096*4: não estoura
    if (bytes > SYS_WIN_BLIT_MAX_BYTES) return (uint64_t)-1;
    if (!validate_user_range(b.pixels_ptr, bytes)) return (uint64_t)-1;

    return userwin_blit(task_current()->id, (int64_t)handle, b.x, b.y, b.w, b.h,
                        (const uint32_t*)b.pixels_ptr) == 0 ? 0 : (uint64_t)-1;
}

// Espera por um evento cedendo a CPU em laço (mesmo padrão de SYS_SLEEP_MS:
// as syscalls entram com IF=0, o tempo avança porque a GUI continua
// rodando). NÃO guarda ponteiro de janela entre as voltas — a cada
// iteração userwin_poll refaz o lookup pelo handle, pois o usuário pode
// fechar a janela (X) enquanto estamos cedendo a CPU.
static uint64_t sys_win_event(uint64_t handle, uint64_t user_out_ptr, uint64_t timeout_ms) {
    if (!validate_user_range_writable(user_out_ptr, sizeof(haos_win_event_t)))
        return (uint64_t)-1;

    uint64_t id = task_current()->id;
    int forever = (timeout_ms == SYS_WIN_WAIT_FOREVER);
    uint64_t deadline = 0;
    if (!forever) {
        if (timeout_ms > SYS_SLEEP_MAX_MS) timeout_ms = SYS_SLEEP_MAX_MS;
        deadline = timer_ticks + (timeout_ms + SYS_MS_PER_TICK - 1) / SYS_MS_PER_TICK;
    }

    for (;;) {
        haos_win_event_t ev;
        int r = userwin_poll(id, (int64_t)handle, &ev);
        if (r < 0) return (uint64_t)-1;                 // janela fechada/inexistente
        if (r > 0) {
            kmemcpy((void*)user_out_ptr, &ev, sizeof(ev));
            return 1;
        }
        if (!forever && timer_ticks >= deadline) return 0;  // timeout (0 = só consulta)
        scheduler_yield();
    }
}

static uint64_t sys_win_info(uint64_t handle, uint64_t user_out_ptr) {
    if (!validate_user_range_writable(user_out_ptr, sizeof(haos_win_info_t)))
        return (uint64_t)-1;
    haos_win_info_t info;
    if (userwin_info(task_current()->id, (int64_t)handle, &info) != 0) return (uint64_t)-1;
    kmemcpy((void*)user_out_ptr, &info, sizeof(info));
    return 0;
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a5; // SYS_SPAWN usa até a4; nenhuma syscall usa a5 ainda

    switch (num) {
        case SYS_WRITE: return sys_write(a1, a2);
        case SYS_EXIT:  return sys_exit(a1); // não retorna
        case SYS_SBRK:  return sys_sbrk((int64_t)a1);
        case SYS_OPEN:  return sys_open(a1, a2, a3);
        case SYS_READ:  return sys_read(a1, a2, a3);
        case SYS_CLOSE: return sys_close(a1);
        case SYS_WRITE_FD: return sys_write_fd(a1, a2, a3);
        case SYS_SPAWN: return sys_spawn(a1, a2, a3, a4);
        case SYS_WAITPID: return sys_waitpid(a1, a2);
        case SYS_GETPID:    return sys_getpid();
        case SYS_YIELD:     return sys_yield();
        case SYS_UPTIME_MS: return sys_uptime_ms();
        case SYS_SLEEP_MS:  return sys_sleep_ms(a1);
        case SYS_STAT:      return sys_stat(a1, a2, a3);
        case SYS_READDIR:   return sys_readdir(a1, a2, a3, a4);
        case SYS_GETCWD:    return sys_getcwd(a1, a2);
        case SYS_CHDIR:     return sys_chdir(a1, a2);
        case SYS_MKDIR:     return sys_mkdir(a1, a2);
        case SYS_UNLINK:    return sys_unlink(a1, a2);
        case SYS_LSEEK:     return sys_lseek(a1, a2, a3);
        case SYS_WIN_CREATE:  return sys_win_create(a1, a2, a3, a4);
        case SYS_WIN_DESTROY: return sys_win_destroy(a1);
        case SYS_WIN_FILL:    return sys_win_fill(a1, a2);
        case SYS_WIN_TEXT:    return sys_win_text(a1, a2);
        case SYS_WIN_BLIT:    return sys_win_blit(a1, a2);
        case SYS_WIN_EVENT:   return sys_win_event(a1, a2, a3);
        case SYS_WIN_INFO:    return sys_win_info(a1, a2);
        case SYS_GETUID:      return sys_getuid();
        case SYS_GETGID:      return sys_getgid();
        case SYS_CHMOD:       return sys_chmod(a1, a2, a3);
        case SYS_GETRANDOM:   return sys_getrandom(a1, a2);
        case SYS_RENAME:      return sys_rename(a1, a2, a3, a4);
        default:        return (uint64_t)-1; // syscall desconhecida
    }
}
