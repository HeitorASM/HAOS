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
    bool            is_user_task;           // true = processo Ring 3
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
    bool            has_exited;             // true assim que sys_exit
                                             // for chamado — o terminal
                                             // usa isso para mostrar
                                             // "processo encerrado" uma
                                             // única vez, mesmo que a
                                             // task ainda esteja
                                             // TASK_ZOMBIE aguardando o
                                             // reaper.
    uint64_t        exit_code;              // código passado a sys_exit,
                                             // válido quando has_exited.

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

// Task atualmente em execução (NULL antes de tasking_init()).
task_t* task_current(void);

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

#ifdef __cplusplus
}
#endif
