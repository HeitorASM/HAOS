// ============================================================
//  kernel/scheduler.c — Round-Robin simples, dirigido pelo PIT
// ============================================================
#include "scheduler.h"
#include "task.h"
#include "gdt.h"
#include "paging.h"

extern void task_switch(uint64_t* old_rsp_ptr, uint64_t new_rsp);

static task_t*  s_queue_head = NULL;  // fila circular: head->next->...->head
static uint64_t s_ticks_left = SCHEDULER_QUANTUM_TICKS;
static bool     s_scheduler_ready = false; // false até scheduler_init() rodar

void scheduler_init(void) {
    task_t* t0 = task_current();
    s_queue_head = t0; // fila circular de 1 elemento (t0->next == t0, já
                        // montado em tasking_init())
    s_ticks_left = SCHEDULER_QUANTUM_TICKS;
    s_scheduler_ready = true; // só a partir daqui scheduler_tick() pode agir
}

void scheduler_add(task_t* t) {
    if (!s_queue_head) {
        // não deveria acontecer se scheduler_init() já rodou, mas
        // não custa ser defensivo nesta etapa inicial.
        s_queue_head = t;
        t->next = t;
        return;
    }
    // Insere logo depois da task atual, antes do resto da fila —
    // simples, e garante que uma task recém-criada roda "logo",
    // sem esperar dar a volta inteira na fila se ela for grande.
    task_t* cur = task_current();
    t->next = cur->next;
    cur->next = t;
}

// ---- BUG CRÍTICO CORRIGIDO AQUI ----
// Remove `t` da fila circular do scheduler, percorrendo a partir de
// s_queue_head até achar quem aponta para `t` e religando esse
// ponteiro para t->next (pulando t). Chamado pelo reaper
// (task_reap_zombies, em task.c) ao colher uma task ZOMBIE.
//
// SEM ISSO, uma task cujo slot é RECICLADO (reaproveitado por um
// process_create/task_create seguinte) permanecia com seu ponteiro
// .next antigo intacto na fila — e quando scheduler_add() era
// chamado de novo para essa MESMA struct de task (mesmo endereço de
// memória, reaproveitado), o cálculo "t->next = cur->next" podia
// produzir "t->next = t" (self-loop) sempre que cur->next ainda
// apontasse para a própria t de uma inserção anterior nunca desfeita.
// Isso corrompia a fila circular de forma silenciosa: pick_next(t)
// via t->next == t imediatamente e retornava a própria t sem nunca
// alcançar o resto da fila (inclusive a task 0) — e como isso
// acontecia bem no momento em que o SEGUNDO processo de usuário
// tentava ceder a CPU via sys_exit()/scheduler_yield(), o sistema
// travava exatamente ali: do_switch_to via next==prev e não fazia
// nada, deixando a execução continuar num caminho marcado _Noreturn
// que nunca deveria retornar — comportamento indefinido, sem
// nenhuma exceção de CPU para disparar o kernel panic. Isso batia
// exatamente com o sintoma relatado: primeiro processo sempre
// funcionava (a fila ainda estava íntegra), segundo travava sempre
// (a fila já estava corrompida pelo primeiro ciclo completo).
void scheduler_remove(task_t* t) {
    if (!s_queue_head) return;

    if (t->next == t) {
        // Era o único elemento da fila. Isso só pode acontecer de
        // forma segura se `t` não for a task 0 (que nunca é
        // removida) — mas por segurança, se s_queue_head for a
        // própria `t`, não temos para onde apontar depois de
        // removê-la; deixamos a fila vazia (NULL) nesse caso
        // extremo, embora isso não deva ocorrer na prática (task 0
        // sempre existe e nunca é reciclada).
        if (s_queue_head == t) s_queue_head = NULL;
        return;
    }

    task_t* iter = t->next;
    while (iter->next != t) {
        iter = iter->next;
        if (iter == t) return; // `t` não estava na fila (não deveria
                                 // acontecer, mas evita loop infinito
                                 // se os ponteiros já estiverem
                                 // inconsistentes por outro motivo).
    }
    iter->next = t->next; // pula `t`, religando quem apontava para
                            // ela direto para o que vinha depois dela.
    if (s_queue_head == t) s_queue_head = t->next;
    t->next = NULL; // marca como "fora da fila" — scheduler_add()
                      // vai preenchê-lo de novo se este slot for
                      // reaproveitado no futuro.
}

// Escolhe a PRÓXIMA task READY na fila circular a partir de `from`.
// Pula tasks ZOMBIE/BLOCKED. Se não achar nenhuma outra candidata,
// retorna a própria `from` (sistema com uma task só rodando, ou
// todas as outras bloqueadas — continuar rodando a atual é o
// comportamento correto aqui, não um bug).
static task_t* pick_next(task_t* from) {
    task_t* candidate = from->next;
    while (candidate != from) {
        if (candidate->state == TASK_READY || candidate->state == TASK_RUNNING) {
            return candidate;
        }
        candidate = candidate->next;
    }
    return from;
}

static void do_switch_to(task_t* next) {
    task_t* prev = task_current();
    if (next == prev) return; // nada a fazer — só a própria task disponível

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;

    task_set_current(next);
    s_ticks_left = SCHEDULER_QUANTUM_TICKS;

    // ---- BUG CRÍTICO CORRIGIDO AQUI ----
    // TSS.RSP0 é o RSP que a CPU carrega AUTOMATICAMENTE sempre que
    // uma interrupção/exceção/syscall ocorre enquanto o processador
    // está em Ring 3. Antes desta correção, só process_bootstrap()
    // (process.c) atualizava isso, e SÓ na primeira execução de um
    // processo — depois que o scheduler trocasse de volta para
    // qualquer outra task (a task 0/GUI, ou outra kernel thread) e
    // depois de volta para um processo de usuário, o RSP0 continuava
    // apontando para a pilha de kernel do processo ERRADO.
    //
    // SEGUNDO BUG, encontrado depois de corrigir o primeiro: a TASK 0
    // (kernel_main/GUI) tem stack_base == 0 (ela usa a pilha original
    // montada pelo boot.asm, nunca alocada por task_alloc_kernel_stack_for
    // — ver tasking_init em task.c). Uma primeira versão desta correção
    // fazia "tss_set_kernel_stack(next->stack_base + TASK_STACK_SIZE)"
    // incondicionalmente — para a task 0 isso calculava
    // tss_set_kernel_stack(0 + TASK_STACK_SIZE), ou seja, RSP0 apontando
    // para o endereço baixíssimo 0x4000 (região crítica de BIOS/boot,
    // não reservada para isso). Isso não quebrava nada na hora (a task 0
    // roda em Ring 0 e não usa RSP0 diretamente), mas deixava a TSS
    // "envenenada": assim que QUALQUER processo de usuário entrasse em
    // Ring 3 depois disso e uma interrupção ou syscall ocorresse, a CPU
    // tentaria empilhar o frame de interrupção em 0x4000 — corrompendo
    // memória crítica de baixo nível e travando o sistema inteiro sem
    // nenhuma exceção detectável. Isso bate exatamente com o sintoma
    // relatado: primeiro processo funciona (RSP0 nunca tinha sido levado
    // a 0x4000 antes), volta para a task 0 (RSP0 é envenenado aqui, mas
    // nada quebra ainda), segundo processo entra em Ring 3 e a primeira
    // interrupção/syscall dele corrompe tudo — trava total, sem panic.
    //
    // Correção: só atualizamos RSP0 quando a task que assume a CPU tem
    // uma pilha de kernel de verdade (stack_base != 0). Para a task 0,
    // simplesmente não mexemos em RSP0 — ela nunca entra em Ring 3, então
    // não precisa de um RSP0 válido para si mesma; o valor que ficar lá
    // (do último processo de usuário que rodou) é irrelevante enquanto
    // só processos de usuário derem IRETQ/SYSCALL usando RSP0.
    if (next->stack_base != 0) {
        tss_set_kernel_stack(next->stack_base + TASK_STACK_SIZE);
    }

    // ---- Fase 3: troca de espaço de endereço (CR3) ----
    // Kernel threads e a task 0 têm address_space == NULL, o que
    // paging_switch_to() interpreta como "o espaço do kernel" — ou
    // seja, tasks sem processo de usuário sempre rodam no espaço do
    // kernel (comportamento idêntico ao que já existia antes da Fase
    // 3). Processos de usuário têm seu próprio address_space_t, e
    // trocar para ele aqui é o que de fato isola a memória de um
    // processo da de outro: a partir deste load_cr3, qualquer
    // endereço de usuário que a CPU traduzir (inclusive por causa de
    // uma instrução do próprio processo em Ring 3, ou por um ponteiro
    // de usuário que uma syscall precise ler) passa pelas tabelas
    // EXCLUSIVAS deste processo, não mais pelas de qualquer outro que
    // tenha rodado antes.
    paging_switch_to(next->address_space);

    // A PARTIR DAQUI a execução "some" e só volta para a linha
    // seguinte quando ALGUÉM escalonar `prev` de novo no futuro —
    // pode ser dali a alguns ticks (caso normal) ou nunca, se
    // `prev` era uma task que acabou de chamar task_exit() (nesse
    // caso o "retorno" nunca é usado, e tudo bem).
    task_switch(&prev->rsp, next->rsp);
}

void scheduler_tick(void) {
    // IRQ0 já está habilitada bem antes de tasking_init()/scheduler_init()
    // rodarem (kernel_main dá "sti" logo após pit_init(), mas só monta a
    // fila do scheduler muito depois, já perto do fim da inicialização).
    // Sem essa guarda, os primeiros ticks do PIT chamariam
    // pick_next(NULL) e desreferenciariam ponteiro nulo dentro de uma
    // interrupção — travando o boot inteiro em silêncio, antes mesmo do
    // framebuffer aparecer (foi exatamente o bug que causou tela preta
    // total reportado ao testar no VirtualBox). Ticks que chegarem antes
    // da inicialização são simplesmente ignorados; nada se perde, porque
    // ainda não existe nenhuma task além da que já está rodando.
    if (!s_scheduler_ready) return;

    if (s_ticks_left > 0) {
        s_ticks_left--;
        return;
    }
    task_t* next = pick_next(task_current());
    do_switch_to(next);
}

void scheduler_yield(void) {
    task_t* next = pick_next(task_current());
    do_switch_to(next);
}
