#pragma once
#include "types.h"
#include "paging.h"

// ============================================================
//  kernel/task.h — Estrutura de task (processo/thread de kernel)
//
//  Etapa 1 do scheduler: só kernel threads (Ring 0), sem espaço
//  de endereço próprio ainda (todas compartilham o mesmo CR3 do
//  kernel, identity-mapped). Isso isola o problema de context
//  switch puro antes de somar a complexidade de paginação por
//  processo (que entra na etapa 2, junto com o loader de
//  executáveis de usuário).
//
//  Cada task tem sua própria pilha de kernel, alocada página por
//  página via pfa_alloc_frame + paging_map (não via kmalloc: isso
//  permite reservar uma GUARD PAGE desmapeada logo abaixo da
//  pilha real — ver task_create). Um overflow de pilha vira um
//  #PF de verdade, tratado pelo handler existente em isr.c, em vez
//  de corromper silenciosamente a memória vizinha.
//
//  O "contexto salvo" na troca é mínimo: só os registradores
//  callee-saved (RBX, RBP, R12-R15) + RSP + RIP de retorno. Não
//  precisamos salvar os registradores caller-saved (RAX, RCX,
//  RDX, RSI, RDI, R8-R11) porque a troca de contexto acontece
//  dentro de uma chamada de função (task_switch), e a convenção
//  de chamada do System V AMD64 ABI já garante que o CHAMADOR
//  salva o que precisa antes de chamar — o switch só precisa
//  preservar o que a ABI diz que o CALLEE deve preservar.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define TASK_NAME_MAX      32
#define TASK_STACK_PAGES    4            // 4 páginas de 4KB = 16KB de pilha
                                          // utilizável por task, suficiente
                                          // para chamadas aninhadas normais
                                          // do kernel. Uma 5ª página é
                                          // reservada e DESMAPEADA logo
                                          // abaixo, como guard page — ver
                                          // task_create(). Aumentar
                                          // TASK_STACK_PAGES se algum
                                          // handler recursivo estourar isso
                                          // (o #PF da guard page vai
                                          // denunciar exatamente qual task
                                          // e onde, em vez de corromper
                                          // memória vizinha em silêncio).
#define TASK_STACK_SIZE    (TASK_STACK_PAGES * PAGE_SIZE_4K)
#define TASK_MAX_COUNT      16           // limite fixo por simplicidade
                                          // nesta etapa (array estático,
                                          // sem alocação dinâmica da
                                          // tabela de tasks).
#define TASK_MAX_OPEN_FILES  8           // arquivos abertos simultaneamente
                                          // por processo — ver fd_table
                                          // abaixo. Suficiente para um
                                          // programa simples; aumentar se
                                          // algum programa real precisar
                                          // de mais.

// Forward declaration — task.h não inclui fs/vfs.h de propósito, para
// não criar uma dependência do subsistema de tasks (kernel/) sobre o
// VFS (fs/): task_t só guarda PONTEIROS opacos para VfsNode (ver
// fd_table), nunca acessa seus campos internos diretamente. Quem
// precisa dos detalhes de VfsNode (kernel/syscall.c, ao implementar
// sys_open/sys_read/sys_close) inclui fs/vfs.h por conta própria.
struct VfsNode;

typedef enum {
    TASK_UNUSED = 0,   // slot livre na tabela, pronto para reuso
    TASK_READY,        // pronta para rodar, na fila do scheduler
    TASK_RUNNING,      // é a task atualmente executando na CPU
    TASK_BLOCKED,      // esperando algo (ainda não usado nesta etapa)
    TASK_ZOMBIE        // terminou (task_exit); recursos ainda não
                        // liberados — aguardando o reaper (ver
                        // task_reap_zombies) rodar de FORA desta task,
                        // já que ela não pode liberar a própria pilha
                        // enquanto ainda está em cima dela.
} task_state_t;

// Contexto salvo no momento de uma troca de task — layout tem que
// bater EXATAMENTE com a ordem de push/pop em task_switch.asm.
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rip;   // endereço de retorno (onde a task estava quando saiu da CPU)
} task_context_t;

// Bit alto de fd_offset[] (campo de task_t, abaixo) = "este fd foi
// aberto com O_WRONLY" (ver sys_open em syscall.c). Definido aqui,
// fora da struct, para ser visível tanto de kernel/syscall.c (que o
// usa para decidir se sys_write_fd tem permissão de escrever) quanto
// de kernel/task.c (que o usa no reaper — task_reap_zombies — para
// saber se precisa sincronizar com disco antes de fechar um fd que o
// processo deixou aberto ao terminar; ver vfs_sync() ali).
#define FD_WRITABLE_BIT 0x80000000u
#define FD_OFFSET_MASK  0x7FFFFFFFu

typedef struct task {
    uint64_t        id;                     // PID interno (índice + geração)
    task_state_t    state;
    char            name[TASK_NAME_MAX];

    uint64_t        rsp;                    // topo da pilha de kernel NO MOMENTO
                                             // da troca — é o único campo que
                                             // task_switch.asm lê/escreve
                                             // diretamente; task_context_t vive
                                             // EMPILHADO nessa pilha, não como
                                             // struct separada.

    uint64_t        stack_base;             // endereço virtual da PRIMEIRA
                                             // página UTILIZÁVEL da pilha
                                             // (logo acima da guard page).
                                             // task_reap_zombies() desmapeia
                                             // e libera exatamente
                                             // TASK_STACK_PAGES páginas a
                                             // partir daqui. NÃO inclui a
                                             // guard page em si (que nunca
                                             // chegou a ser mapeada, então
                                             // não há nada físico para
                                             // liberar nela).
    uint64_t        guard_page_addr;        // endereço virtual da guard
                                             // page (sempre desmapeada; só
                                             // guardado para diagnóstico/
                                             // simetria, nunca passado a
                                             // paging_unmap).

    // ---- Campos exclusivos de PROCESSOS DE USUÁRIO (Fase 2) ----
    // Para kernel threads (Fase 1), is_user_task é false e todos os
    // campos abaixo ficam zerados/ignorados.
    bool32_t        is_user_task;           // true = processo Ring 3
                                             // criado por process_create();
                                             // false = kernel thread pura
                                             // (Fase 1, task_create()).
    uint64_t        user_entry;             // RIP inicial em Ring 3 (só
                                             // usado na primeira execução,
                                             // via task_bootstrap_user).
    uint64_t        user_stack_top;         // topo da pilha de USUÁRIO
                                             // (RSP inicial em Ring 3).
    uint64_t        user_image_base;        // endereço virtual onde o
                                             // executável foi carregado —
                                             // guardado para o loader poder
                                             // liberar as páginas de
                                             // código/dados quando a task
                                             // for colhida pelo reaper.
    uint64_t        user_image_pages;       // quantas páginas o executável
                                             // ocupa a partir de
                                             // user_image_base (código +
                                             // dados/bss).
    uint64_t        user_stack_base;        // endereço virtual da pilha
                                             // de USUÁRIO (para o reaper
                                             // liberar; separado da pilha
                                             // de KERNEL, que é a mesma
                                             // guard-page-protegida de
                                             // sempre — todo processo de
                                             // usuário também tem uma
                                             // pilha de kernel para tratar
                                             // interrupções/syscalls em
                                             // Ring 0, ver process.c).
    uint64_t        user_stack_pages;       // páginas da pilha de usuário.

    // ---- Espaço de endereço isolado (Fase 3) ----
    // NULL para kernel threads e para a task 0 (todas rodam no
    // espaço de endereço do KERNEL, sempre visível a partir de
    // qualquer address_space_t — ver paging_create_address_space).
    // Para processos de usuário, aponta para um PML4 próprio criado
    // por process_create() via paging_create_address_space(): cada
    // processo agora tem seu próprio espaço de usuário (metade baixa
    // do PML4) totalmente isolado dos demais, o que permite múltiplos
    // processos existirem ao mesmo tempo sem colidir no mesmo
    // endereço de carga (HEXEC_LOAD_ADDR) — a limitação de "só um
    // processo de usuário por vez" documentada em hexec.h deixa de
    // ser necessária a partir desta task ter isso preenchido.
    address_space_t* address_space;

    // ---- Heap de usuário (sbrk) — base do malloc/free da libc ----
    // Região de memória que cresce SOB DEMANDA via sys_sbrk(), à
    // semelhança de brk()/sbrk() do Unix clássico: o kernel só
    // reserva o ENDEREÇO inicial aqui (escolhido logo após a pilha
    // de usuário, dentro da mesma entrada de PDPT exclusiva do
    // processo — ver comentário em process.c sobre essa dependência
    // com paging_create_address_space), e mapeia páginas físicas
    // conforme sys_sbrk pede para crescer. malloc()/free() na libc
    // (ver libc/src/stdlib/malloc.c) implementam um alocador simples
    // por cima desse espaço bruto — o kernel não sabe nada sobre
    // "blocos alocados", só sobre "até onde a heap deste processo já
    // foi mapeada".
    uint64_t        heap_base;              // endereço virtual fixo
                                             // de início da heap
                                             // (constante por
                                             // processo, calculado em
                                             // process_create).
    uint64_t        heap_current;           // até onde a heap já foi
                                             // efetivamente mapeada —
                                             // sys_sbrk(0) retorna
                                             // isso; incrementos
                                             // mapeiam mais páginas a
                                             // partir daqui.
    uint64_t        heap_limit;             // limite máximo que a
                                             // heap pode crescer
                                             // (previne um processo
                                             // consumir toda a
                                             // memória física do
                                             // sistema por engano ou
                                             // de propósito).

    // ---- Descritores de arquivo (Fase de syscalls de I/O) ----
    // Tabela pequena e de tamanho fixo — suficiente para um programa
    // simples abrir alguns arquivos por vez; não há necessidade de
    // crescer dinamicamente nesta fase. Índice 0 é deliberadamente
    // deixado vazio (NULL) nesta versão: não existem ainda
    // descritores 0/1/2 (stdin/stdout/stderr) no sentido POSIX — a
    // saída de sys_write vai sempre para o canal de stdout do
    // processo (stdout_ring, abaixo), não para um "fd 1" de verdade.
    // Isso é uma simplificação deliberada; unificar com um esquema
    // de FDs completo (stdin/stdout/stderr como fds 0/1/2 de
    // verdade) é uma evolução futura razoável.
    struct VfsNode* fd_table[TASK_MAX_OPEN_FILES];
    uint32_t        fd_offset[TASK_MAX_OPEN_FILES]; // posição de leitura/
                                                       // escrita atual de
                                                       // cada fd aberto.
                                                       // O bit mais alto
                                                       // (FD_WRITABLE_BIT,
                                                       // definido acima
                                                       // desta struct)
                                                       // marca "aberto com
                                                       // O_WRONLY" — ver
                                                       // syscall.c. Os 31
                                                       // bits baixos são o
                                                       // offset de verdade
                                                       // (VFS_FILE_MAX é 1
                                                       // MB, cabe folgado).

    // ---- Diretório de trabalho (cwd) por processo ----
    // Base para resolver caminhos RELATIVOS em sys_open/sys_stat/
    // sys_chdir/etc. Herdado do terminal que lançou o processo (run)
    // ou do pai (spawn) — ver process_create_with_cwd. NULL significa
    // "raiz" (kernel threads, task 0). Pode apontar para um nó já
    // removido (VfsNode::removed): quem usa deve passar por task_cwd()
    // em syscall.c, que cai para a raiz nesse caso. Os nós do VFS
    // nunca são liberados, então o ponteiro nunca fica inválido.
    struct VfsNode* cwd;

    // ---- Canal de saída (stdout) para o terminal que o lançou ----
    // Substitui a abordagem anterior de sys_write desenhar direto no
    // framebuffer global: isso competia visualmente com a GUI (o
    // desktop redesenha por cima a cada frame, fazendo a saída do
    // processo "piscar e sumir") e não tinha nenhuma noção de qual
    // janela deveria receber aquele texto. Agora sys_write copia os
    // bytes para este buffer circular pequeno, e o terminal dono
    // (stdout_owner) drena e imprime como texto normal a cada frame
    // do seu próprio loop de eventos — sem qualquer sincronização
    // complexa: o buffer é só lido pela task 0 (dona de todos os
    // terminais) e escrito pelo processo via syscall, nunca os dois
    // ao mesmo tempo em SMP porque este projeto é single-core.
    void*           stdout_owner;           // ponteiro opaco para o
                                             // TermState do terminal
                                             // que criou este processo
                                             // (NULL = nenhum terminal
                                             // associado; saída é
                                             // descartada nesse caso).
    uint8_t         stdout_ring[256];       // buffer circular de saída
    uint16_t        stdout_head;            // próxima posição de escrita
    uint16_t        stdout_tail;            // próxima posição de leitura

    // ---- "Segurar" um zumbi para SYS_WAITPID ----
    //
    // O reaper (task_reap_zombies) só recicla uma task TASK_ZOMBIE
    // quando NENHUM lado ainda precisa ler o resultado dela — hoje
    // isso já vale para stdout_owner (o terminal precisa mostrar a
    // mensagem final antes do slot sumir). waiter_pid é a mesma ideia
    // do lado de um processo PAI que chamou sys_waitpid(child_pid) e
    // ainda não recebeu o exit_code: enquanto waiter_pid == o id de
    // um processo esperando, o reaper NÃO recicla esta task, mesmo
    // que stdout_owner já tenha sido solto pelo terminal (caso comum:
    // um filho em background termina, term_update_bg_processes já
    // soltou stdout_owner no frame seguinte, mas o pai ainda não
    // rodou seu waitpid()).
    //
    // Por que um id (uint64_t) e não um task_t* direto para o pai:
    // se o PAI morresse antes de chamar waitpid (crash, kill, etc.),
    // um ponteiro ficaria pendurado apontando para um slot que o
    // reaper já reciclou para outra task completamente diferente.
    // Um id nunca "reaparece" (s_next_id só cresce — ver task.c),
    // então comparar id é seguro mesmo que o slot original do pai
    // já tenha sido reciclado.
    uint64_t        waiter_pid;             // 0 = ninguém esperando por
                                             // esta task; caso contrário,
                                             // id do processo que chamou
                                             // sys_waitpid nela e ainda
                                             // não recebeu o resultado.

    bool32_t        has_exited;             // true assim que sys_exit
                                             // for chamado — o terminal
                                             // usa isso para mostrar
                                             // "processo encerrado" uma
                                             // única vez, mesmo que a
                                             // task ainda esteja
                                             // TASK_ZOMBIE aguardando o
                                             // reaper.
    uint64_t        exit_code;              // código passado a sys_exit,
                                             // válido quando has_exited.

    // ---- Canal de entrada (stdin) vindo do terminal dono ----
    // Espelha stdout_ring, mas na direção oposta: o terminal
    // (stdout_owner) é quem PRODUZ aqui, byte a byte, conforme o
    // usuário digita, ENQUANTO este processo estiver "em foco" nele
    // (running_process == esta task); sys_read(fd=0,...) é o
    // CONSUMIDOR, chamado a partir da própria task via syscall. Ao
    // contrário de stdout_ring, aqui NÃO sobrescrevemos em caso de
    // buffer cheio — input do usuário não pode ser silenciosamente
    // perdido como uma linha de log pode; em vez disso
    // task_stdin_write() simplesmente descarta bytes NOVOS que não
    // caibam (equivalente a "buffer de teclado cheio, tecla
    // ignorada", situação rara com um ring de 256 bytes e digitação
    // humana).
    uint8_t         stdin_ring[256];
    uint16_t        stdin_head;             // próxima posição de escrita (produtor: terminal)
    uint16_t        stdin_tail;             // próxima posição de leitura (consumidor: sys_read)
    bool32_t        waiting_for_stdin;       // true quando esta task está
                                             // TASK_BLOCKED especificamente
                                             // por falta de dado em stdin
                                             // (distingue de outros motivos
                                             // de bloqueio que possam existir
                                             // no futuro) — task_stdin_write()
                                             // usa isso para saber quando
                                             // precisa desbloquear a task.

    struct task*    next;                   // fila circular do scheduler (round-robin)
} task_t;

// ---- API pública ----

// Inicializa o subsistema de tasks: cria a "task 0" representando
// o próprio fluxo de execução atual do kernel_main (a task que
// está rodando quando scheduler_init() é chamada vira a primeira
// TASK_RUNNING, sem precisar de uma pilha nova alocada para ela).
void tasking_init(void);

// Cria uma nova kernel thread: aloca pilha própria com guard page,
// monta o contexto inicial para que a primeira troca "retorne"
// para entry_point (chamada com argumento arg), e insere na fila
// do scheduler como TASK_READY. Retorna o ponteiro da task criada,
// ou NULL se a tabela de tasks estiver cheia ou a alocação da
// pilha falhar (nesse caso, nenhuma página fica vazando: qualquer
// frame já alocado antes da falha é liberado antes de retornar).
typedef void (*task_entry_fn)(void* arg);
task_t* task_create(const char* name, task_entry_fn entry_point, void* arg);

// Chamado pela própria kernel thread quando termina (ou por quem
// a criou, para encerrá-la à força). Marca como ZOMBIE e força uma
// troca de contexto imediata — NUNCA RETORNA. A pilha da task NÃO
// é liberada aqui (ver task_reap_zombies).
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void task_exit(void);

// ---- Encerramento de uma task DE FORA (por outra task) ----
//
// Diferente de task_exit() (chamado pela PRÓPRIA task ao terminar
// normalmente), task_kill() é chamado por OUTRA task — tipicamente
// a task 0/GUI, quando o usuário aperta Ctrl+C num terminal com um
// processo rodando (ver on_event em gui/apps/terminal.cpp) — para
// encerrar `t` à força, sem que `t` tenha cooperado ou sequer
// esperado isso.
//
// Por isso NÃO pode fazer o que task_exit() faz (scheduler_yield()
// "sumindo" da execução atual): quem chama task_kill() é uma task
// DIFERENTE de `t`, que precisa CONTINUAR rodando normalmente depois
// da chamada — só marcamos `t` como TASK_ZOMBIE e a removemos da
// fila do scheduler (scheduler_remove), garantindo que ela nunca
// mais seja escalonada. A liberação de memória de verdade continua
// acontecendo depois, pelo reaper (task_reap_zombies), exatamente
// como para uma task que terminou sozinha — task_kill() só marca
// "isto deve morrer", não libera nada aqui.
//
// Seguro de chamar mesmo se `t` estava TASK_BLOCKED (ex.: dormindo
// num sys_read de stdin — ver waiting_for_stdin) — o estado bloqueado
// é irrelevante depois de virar ZOMBIE, e o processo nunca vai
// "acordar" de volta porque foi removido da fila do scheduler.
//
// NÃO chame isto com `t == task_current()` — para a própria task se
// matar, use task_exit(), que faz a troca de contexto necessária;
// task_kill() não faz isso e deixaria a task atual em estado
// inconsistente (ZOMBIE mas ainda em execução).
void task_kill(task_t* t);

// ---- Gancho de "adoção" de processos filhos (SYS_SPAWN) ----
//
// Quando um processo cria outro via sys_spawn, o filho precisa ser
// "adotado" pelo terminal dono do PAI, para que a saída dele apareça
// lá (o terminal só drena as tasks que ele conhece). O kernel (C) não
// conhece TermState (C++, camada de GUI), então o terminal REGISTRA
// aqui uma função que faz a adoção: recebe o `stdout_owner` do pai
// (opaco, void*) e o filho, e devolve true se conseguiu adotar (ex.:
// havia slot livre em bg_processes) ou false se não (o kernel então
// desfaz o spawn matando o filho).
typedef bool (*task_adopt_child_fn)(void* owner, task_t* child);
void task_set_adopt_child_hook(task_adopt_child_fn fn);
task_adopt_child_fn task_get_adopt_child_hook(void);

// Task atualmente em execução (NULL antes de tasking_init()).
task_t* task_current(void);

// Procura uma task pelo id (task_t::id, atribuído sequencialmente em
// task_create/process_create — nunca reutilizado, ver s_next_id em
// task.c). Devolve NULL se não houver NENHUMA task viva com esse id
// — o que cobre tanto "id nunca existiu" quanto "existiu, terminou, e
// já foi totalmente reciclada pelo reaper" (não dá pra distinguir os
// dois casos só pelo id, mas para sys_waitpid isso é exatamente o
// comportamento certo: se o id não corresponde a nada vivo, não há
// como esperar por ele). Note que uma task TASK_ZOMBIE ainda "viva"
// (aguardando o reaper) É encontrada normalmente — é assim que
// sys_waitpid consegue ler exit_code de um filho que já terminou mas
// ainda não foi reciclado.
task_t* task_find_by_id(uint64_t id);

// true se existe uma task com este id que ainda NÃO terminou (nem
// TASK_UNUSED, nem TASK_ZOMBIE). Ao contrário de task_find_by_id,
// zumbis contam como "mortos" — é o que o código da GUI quer para
// decidir se deve fechar as janelas de um processo (userwin_reap).
bool task_is_alive(uint64_t id);

// Usado internamente pelo scheduler para atualizar qual task é a
// "atual" logo após uma troca de contexto bem-sucedida. Não chame
// isso fora de scheduler.c.
void task_set_current(task_t* t);

// Percorre a tabela de tasks e libera de vez qualquer task em
// estado TASK_ZOMBIE: desmapeia e devolve ao PFA as páginas físicas
// da pilha, e marca o slot como TASK_UNUSED (disponível para um
// novo task_create). Seguro de chamar a qualquer momento, desde que
// NUNCA seja chamado pela própria task que está sendo liberada
// (uma task não pode destruir a pilha em que está executando) — na
// prática, chame isso a partir da task 0 / kernel_main, nunca de
// dentro de uma kernel thread comum. Retorna quantas tasks foram
// efetivamente liberadas nesta chamada.
int task_reap_zombies(void);

// Diagnóstico para o handler de #PF: informa se `addr` cai dentro
// da guard page de alguma task (ver isr.c). out_task_name deve
// apontar para um buffer de pelo menos TASK_NAME_MAX bytes.
bool task_addr_is_guard_page(uint64_t addr, char* out_task_name);

// Informa se existe algum processo de usuário vivo no momento
// (qualquer task com is_user_task=true em estado diferente de
// TASK_UNUSED — ou seja, READY, RUNNING, BLOCKED ou ZOMBIE ainda
// não colhida). Usado por process_create() para respeitar a
// limitação desta fase de só suportar um processo de usuário por
// vez (todos compartilham a mesma janela de endereço virtual fixa
// — ver hexec.h). Evita duplicar esse controle como uma variável
// estática separada em process.c, que ficaria fora de sincronia
// do momento exato em que o reaper de fato libera o slot.
bool task_any_user_process_alive(void);

// ---- API de baixo nível usada por process.c (Fase 2) ----
//
// Reserva um slot TASK_UNUSED da tabela e o marca como reservado
// (protegido internamente contra corrida com scheduler_tick), sem
// tocar em nenhum campo de pilha/contexto — isso fica por conta de
// task_alloc_kernel_stack_for() e de process_create(). Retorna
// NULL se a tabela estiver cheia.
task_t* task_reserve_slot(void);

// Aloca a pilha de KERNEL (com guard page) para a task `t`, que
// deve ter sido obtida de task_reserve_slot() e ainda não ter
// nenhuma pilha montada. Usado tanto internamente por
// task_create() quanto por process_create() (Fase 2) — todo
// processo de usuário também precisa de uma pilha de Ring 0
// própria, para o RSP0 da TSS ter onde atender interrupções e
// syscalls vindas de Ring 3. Preenche *out_stack_top (topo
// utilizável, pronto para receber os primeiros pushes de
// contexto), *out_stack_base e *out_guard_addr (para o reaper
// liberar depois). Retorna false em caso de falha de alocação,
// sem deixar páginas parcialmente mapeadas.
bool task_alloc_kernel_stack_for(task_t* t,
                                  uint64_t* out_stack_top,
                                  uint64_t* out_stack_base,
                                  uint64_t* out_guard_addr);

// Libera (desmapeia + devolve ao PFA) `count` páginas mapeadas
// consecutivamente a partir de `virt_start`. Exposto para
// process.c reusar ao desfazer uma alocação parcial de imagem de
// executável ou pilha de usuário em caso de falha no meio do
// carregamento.
//
// `as`: em qual espaço de endereço as páginas estão mapeadas.
// Passe NULL para o espaço de endereço ATIVO no momento da chamada
// (é o que toda a pilha de KERNEL de qualquer task usa — pilhas de
// kernel vivem sempre no espaço do kernel, nunca no espaço de
// usuário isolado de um processo). Para liberar páginas de
// código/dados/pilha de USUÁRIO de um processo (Fase 3, espaços de
// endereço isolados), passe t->address_space explicitamente — isso
// é necessário porque essas páginas podem estar sendo liberadas
// pelo reaper enquanto rodam a partir da task 0 (GUI), ou seja, com
// o espaço de endereço DAQUELE processo não sendo o ativo no
// momento; usar NULL nesse caso desmapearia do espaço errado.
void task_free_mapped_pages(address_space_t* as, uint64_t virt_start, uint64_t count);

// Publica a task como pronta e a insere na fila do scheduler —
// equivalente à seção final de task_create(), extraída para reuso
// por process_create(). Deve ser chamada só depois que TODOS os
// campos relevantes de `t` (rsp, stack_base, guard_page_addr,
// is_user_task, user_* se aplicável) já estiverem preenchidos.
void task_publish_ready(task_t* t, const char* name);

// ---- Canal de saída (stdout) de processos de usuário ----
//
// Escreve até `len` bytes de `data` no buffer circular de saída de
// `t` (usado por sys_write em syscall.c). Se o buffer estiver
// cheio, os bytes mais antigos ainda não lidos são descartados
// silenciosamente (perder alguns bytes de log é preferível a
// bloquear o processo esperando o terminal ler, o que exigiria uma
// forma de "dormir" que esta fase ainda não tem). Retorna quantos
// bytes foram de fato escritos (sempre `len`, neste esquema de
// sobrescrita — mantido como uint64_t para simetria com o restante
// da API de syscall).
uint64_t task_stdout_write(task_t* t, const uint8_t* data, uint64_t len);

// Lê até `max_len` bytes já escritos por task_stdout_write() e
// ainda não consumidos, copiando para `out_buf`. Usado pelo
// terminal a cada frame para drenar a saída de um processo que ele
// lançou. Retorna quantos bytes foram copiados (pode ser 0 se não
// houver nada novo).
uint64_t task_stdout_read(task_t* t, uint8_t* out_buf, uint64_t max_len);

// ---- Canal de entrada (stdin) de processos de usuário ----
//
// Escreve até `len` bytes de `data` no buffer circular de entrada de
// `t` (chamado pelo terminal a cada tecla digitada, enquanto `t` for
// o running_process em foco). Diferente de task_stdout_write, NÃO
// sobrescreve em caso de buffer cheio — bytes que não couberem são
// descartados sem afetar o que já está no ring (input do usuário não
// deve "empurrar para fora" input mais antigo ainda não lido).
// Retorna quantos bytes foram de fato escritos (pode ser < len se o
// buffer estava quase cheio).
//
// Se `t` estava TASK_BLOCKED esperando stdin (waiting_for_stdin),
// esta função a desbloqueia (volta para TASK_READY) — é assim que um
// sys_read(fd=0) bloqueado "acorda" quando o usuário finalmente digita
// algo.
uint64_t task_stdin_write(task_t* t, const uint8_t* data, uint64_t len);

// Lê até `max_len` bytes já escritos por task_stdin_write() e ainda
// não consumidos, copiando para `out_buf`. Usado por sys_read(fd=0)
// em syscall.c. Retorna quantos bytes foram copiados (0 se não houver
// nada disponível — quem chama decide se isso significa bloquear).
uint64_t task_stdin_read(task_t* t, uint8_t* out_buf, uint64_t max_len);

// ---- Trava de layout: C e C++ têm que concordar sobre task_t ----
//
// task_t é incluída tanto por código C (kernel/task.c, syscall.c,
// process.c, scheduler.c — compilados com -std=c11) quanto por
// código C++ (gui/apps/terminal.cpp, gui/screens/desktop.cpp —
// -std=c++17). As duas linguagens são livres para escolher tamanhos
// DIFERENTES para certos tipos (o caso real que mordeu este projeto:
// `bool` é 1 byte em C++ nativo, mas era 4 bytes no `typedef int
// bool` deste kernel em C — ver o comentário grande em types.h sobre
// bool32_t). Quando isso acontece, um ponteiro `task_t*` idêntico
// aponta para offsets DIFERENTES de um mesmo campo dependendo de
// quem está olhando — sem NENHUM erro de compilação, só corrupção de
// memória silenciosa em runtime (foi assim que a saída de processos
// parou de aparecer numa sessão de debug real deste projeto).
//
// Esta macro captura sizeof(task_t) no ponto em que um arquivo
// específico a expande. Cada arquivo .c E cada arquivo .cpp que
// inclui task.h deve chamar isto UMA VEZ, no escopo de arquivo, com
// um nome único (convenção: task_layout_guard_<nome_do_arquivo>) —
// se o tamanho computado por aquele arquivo não bater com o valor
// gravado aqui (atualizado manualmente sempre que task_t muda de
// verdade), a compilação FALHA imediatamente com uma mensagem
// apontando para este comentário, em vez de deixar o bug se
// manifestar como "a saída do processo sumiu" três camadas de
// abstração depois. Ver kernel/task.c e gui/apps/terminal.cpp para
// os pontos de uso.
#define TASK_T_EXPECTED_SIZE 832

#ifdef __cplusplus
  #define TASK_LAYOUT_GUARD(unique_name) \
      static_assert(sizeof(task_t) == TASK_T_EXPECTED_SIZE, \
          "task_t tem tamanho diferente do esperado neste arquivo - " \
          "provavelmente C e C++ estao discordando sobre o tamanho de " \
          "algum campo (ex.: bool). Veja o comentario acima de " \
          "TASK_LAYOUT_GUARD em kernel/task.h antes de mudar " \
          "TASK_T_EXPECTED_SIZE.")
#else
  #define TASK_LAYOUT_GUARD(unique_name) \
      _Static_assert(sizeof(task_t) == TASK_T_EXPECTED_SIZE, \
          "task_t tem tamanho diferente do esperado neste arquivo - " \
          "provavelmente C e C++ estao discordando sobre o tamanho de " \
          "algum campo (ex.: bool). Veja o comentario acima de " \
          "TASK_LAYOUT_GUARD em kernel/task.h antes de mudar " \
          "TASK_T_EXPECTED_SIZE.")
#endif

#ifdef __cplusplus
}
#endif
