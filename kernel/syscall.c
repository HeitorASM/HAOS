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
#include "process.h"
#include "scheduler.h"
#include "../fs/vfs.h"

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
        // paging_is_user_page checa DUAS coisas de uma vez: (a) a
        // página está mapeada, e (b) foi mapeada com PAGE_USER — ou
        // seja, pertence de fato ao espaço de endereço deste
        // processo, não é uma página de KERNEL que por coincidência
        // tem uma tradução válida (ex.: a própria pilha de kernel da
        // task, ou o código do kernel). Sem o item (b), um processo
        // malicioso poderia passar o endereço de alguma estrutura de
        // kernel conhecida para sys_write/sys_read e usar a syscall
        // como uma forma de ler/vazar memória de kernel arbitrária.
        if (!paging_is_user_page(page)) return false;
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

// Bit alto de fd_offset[] = "este fd foi aberto com O_WRONLY". Guardado
// aqui em vez de um campo novo em task_t porque acrescentar um array
// de bool a task_t fez o kernel quebrar em teste real no QEMU (o
// processo rodava mas a saída das contas nunca aparecia) — a causa
// exata do layout não foi identificada, então evitamos mexer no
// tamanho da struct. VFS_FILE_MAX (1 MB) usa só os 20 bits baixos.
#define FD_WRITABLE_BIT 0x80000000u
#define FD_OFFSET_MASK  0x7FFFFFFFu

static uint64_t sys_open(uint64_t user_path_ptr, uint64_t path_len, uint64_t flags) {
    if (path_len == 0 || path_len >= SYS_OPEN_MAX_PATH) return (uint64_t)-1;
    if (!validate_user_range(user_path_ptr, path_len)) return (uint64_t)-1;

    char path[SYS_OPEN_MAX_PATH];
    kmemcpy(path, (const void*)user_path_ptr, path_len);
    path[path_len] = '\0';

    task_t* t = task_current();
    int fd = find_free_fd(t);
    if (fd < 0) return (uint64_t)-1; // tabela de FDs deste processo cheia

    // Resolve relativo à RAIZ do VFS, não ao cwd do terminal que
    // lançou o processo — um processo de usuário não tem noção de
    // "diretório de trabalho" nesta fase. Programas devem usar
    // caminhos absolutos por enquanto.
    VfsNode* node = vfs_resolve(vfs_root(), path);

    if (!node && (flags & O_CREAT)) {
        // Arquivo não existe e o processo pediu criação: separa o
        // caminho em "diretório pai" + "nome do arquivo" no último
        // '/', resolve o pai (que PRECISA já existir e ser um
        // diretório — não criamos diretórios intermediários) e cria
        // o arquivo dentro dele com vfs_touch.
        int last_slash = -1;
        for (uint64_t i = 0; i < path_len; i++) {
            if (path[i] == '/') last_slash = (int)i;
        }

        VfsNode* parent;
        const char* fname;
        if (last_slash < 0) {
            parent = vfs_root();
            fname  = path;
        } else if (last_slash == 0) {
            parent = vfs_root();       // "/arquivo"
            fname  = path + 1;
        } else {
            path[last_slash] = '\0';   // corta o caminho no último '/'
            parent = vfs_resolve(vfs_root(), path);
            fname  = path + last_slash + 1;
        }

        if (!parent || parent->type != VFS_DIR) return (uint64_t)-1;
        if (fname[0] == '\0') return (uint64_t)-1; // caminho terminava em '/'

        node = vfs_touch(parent, fname);
    }

    if (!node || node->type != VFS_FILE) return (uint64_t)-1;

    // O_TRUNC: só tem efeito se também abriu para escrita — truncar
    // num open somente-leitura seria destrutivo sem que o processo
    // tenha declarado intenção de gravar.
    if ((flags & O_WRONLY) && (flags & O_TRUNC)) {
        node->size = 0;
        if (node->data && node->capacity > 0) node->data[0] = '\0';
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
    if (!validate_user_range(user_buf_ptr, count)) return (uint64_t)-1;

    #define SYS_READ_STDIN_MAX_LEN 64 // uma tecla de cada vez é o caso
                                        // comum; um chunk pequeno também
                                        // mantém a pilha de kernel desta
                                        // syscall previsível, mesma
                                        // disciplina do SYS_READ_MAX_LEN
                                        // de arquivo, abaixo.
    uint8_t buf[SYS_READ_STDIN_MAX_LEN];
    uint64_t want = count < SYS_READ_STDIN_MAX_LEN ? count : SYS_READ_STDIN_MAX_LEN;

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
    if (!validate_user_range(user_buf_ptr, count)) return (uint64_t)-1;

    VfsNode* node = t->fd_table[fd];
    uint32_t offset = t->fd_offset[fd] & FD_OFFSET_MASK;
    if (offset >= node->size) return 0; // fim de arquivo

    uint32_t available = node->size - offset;
    uint64_t total_to_copy = count < available ? count : available;

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

    VfsNode* node = vfs_resolve(vfs_root(), path);
    if (!node || node->type != VFS_FILE) return (uint64_t)-1;

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

    task_t* child = process_create(task_name, node, argv_buf, alen);
    if (!child) return (uint64_t)-1;

    child->stdout_owner = parent->stdout_owner;
    if (!adopt(parent->stdout_owner, child)) {
        child->stdout_owner = NULL;
        task_kill(child);
        return (uint64_t)-1;
    }
    return 1;
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
    task_exit(); // nunca retorna
    __builtin_unreachable();
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
        default:        return (uint64_t)-1; // syscall desconhecida
    }
}
