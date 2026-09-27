// ============================================================
//  kernel/task.c — Tabela de tasks + bootstrap de kernel threads
//                  + primitivas reutilizadas por process.c (Fase 2)
// ============================================================
#include "task.h"
#include "memory.h"
#include "paging.h"
#include "scheduler.h"

extern void task_switch(uint64_t* old_rsp_ptr, uint64_t new_rsp);

static task_t  s_tasks[TASK_MAX_COUNT];
static uint64_t s_next_id = 1;
static task_t* s_current = NULL;

// ---- Região virtual dedicada às pilhas de KERNEL de cada task ----
//
// O kernel usa identity-map (virt == phys) para 0..4GB (ver
// paging.cpp). Para não competir por esse espaço nem por endereços
// já usados pela heap do kmalloc, reservamos uma faixa alta e
// exclusiva para pilhas de kernel de task, começando em 8GB
// virtual. Processos de usuário (Fase 2) têm sua PRÓPRIA região
// separada para código/dados/pilha de usuário (ver process.c) —
// esta faixa aqui é só para a pilha de Ring 0 que toda task
// precisa ter, mesmo as de usuário (é o RSP0 que a TSS usa quando
// uma interrupção/syscall interrompe o processo em Ring 3).
#define TASK_STACK_VREGION_BASE   HAOS_KERNEL_STACK_VREGION_BASE   // ver paging.h — precisa
                                                                     // bater com a região que
                                                                     // paging_init() garante
                                                                     // existir antecipadamente
#define TASK_STACK_SLOT_PAGES     (1 + TASK_STACK_PAGES + 1) // guard + pilha + respiro

static uint64_t stack_slot_base_addr(int slot_index) {
    return TASK_STACK_VREGION_BASE
         + (uint64_t)slot_index * TASK_STACK_SLOT_PAGES * PAGE_SIZE_4K;
}

// Guarda temporariamente entry_point/arg entre task_create() e a
// primeira execução de task_bootstrap() — cada task tem seu par
// próprio, guardado nos dois primeiros "slots" da pilha montada à
// mão (ver comentário em task_create).
typedef struct {
    task_entry_fn entry;
    void*         arg;
} bootstrap_info_t;

// Ponto de entrada real de TODA kernel thread nova. Nunca é
// chamado diretamente por CALL de C — é alcançado por
// task_bootstrap_trampoline (task_switch.asm), que por sua vez é
// alcançado via RET dentro de task_switch() na primeira vez que a
// task é escalonada (ver task_create: o RIP empilhado manualmente
// na pilha inicial aponta para o trampolim). Não pode ser static
// porque precisa de linkage externo visível ao ASM.
void task_bootstrap(bootstrap_info_t* info) {
    task_entry_fn entry = info->entry;
    void* arg = info->arg;
    kfree(info); // já copiamos os dois campos, não precisa mais

    entry(arg);

    // Se a função da thread retornar em vez de chamar task_exit()
    // explicitamente, tratamos como saída normal de qualquer forma
    // — nunca deixamos a execução "cair" para instruções aleatórias
    // depois do bootstrap.
    task_exit();
}

void tasking_init(void) {
    for (int i = 0; i < TASK_MAX_COUNT; i++) {
        s_tasks[i].state = TASK_UNUSED;
        s_tasks[i].is_user_task = false;
    }

    // Task 0 representa o próprio kernel_main em execução no
    // momento desta chamada. Não precisa de pilha alocada nem de
    // contexto montado — ela JÁ está rodando na pilha de boot; só
    // registramos isso para que scheduler_tick() tenha uma task
    // "de onde sair" na primeira troca. Como essa pilha não foi
    // alocada por nós (vem do boot.asm), NUNCA deve ser liberada
    // pelo reaper — stack_base = 0 sinaliza isso (ver
    // task_reap_zombies, que pula qualquer task com stack_base 0).
    task_t* t0 = &s_tasks[0];
    t0->id    = s_next_id++;
    t0->state = TASK_RUNNING;
    kstrncpy(t0->name, "kernel_main", TASK_NAME_MAX - 1);
    t0->name[TASK_NAME_MAX - 1] = '\0';
    t0->rsp             = 0;     // preenchido no primeiro scheduler_tick()
    t0->stack_base       = 0;    // não é liberável — é a pilha de boot
    t0->guard_page_addr  = 0;
    t0->is_user_task     = false;
    t0->user_entry       = 0;
    t0->user_stack_top   = 0;
    t0->user_image_base  = 0;
    t0->user_image_pages = 0;
    t0->user_stack_base  = 0;
    t0->user_stack_pages = 0;
    t0->address_space    = NULL;
    t0->heap_base        = 0;
    t0->heap_current     = 0;
    t0->heap_limit       = 0;
    for (int fd = 0; fd < TASK_MAX_OPEN_FILES; fd++) {
        t0->fd_table[fd]  = NULL;
        t0->fd_offset[fd] = 0;
    }
    t0->stdout_owner     = NULL;
    t0->stdout_head      = 0;
    t0->stdout_tail      = 0;
    t0->has_exited       = false;
    t0->exit_code        = 0;
    t0->next             = t0;   // fila circular de 1 elemento por enquanto

    s_current = t0;
}

task_t* task_current(void) {
    return s_current;
}

// Usado pelo scheduler para atualizar qual é a task "atual" sem
// expor s_current diretamente (o scheduler decide QUEM roda; o
// task.c só guarda o estado).
void task_set_current(task_t* t) {
    s_current = t;
}

static task_t* find_free_slot(int* out_index) {
    for (int i = 0; i < TASK_MAX_COUNT; i++) {
        if (s_tasks[i].state == TASK_UNUSED) {
            if (out_index) *out_index = i;
            return &s_tasks[i];
        }
    }
    return NULL;
}

// Encontra o índice de uma task dentro de s_tasks — usado para
// calcular o slot de pilha de kernel a partir do próprio ponteiro
// (necessário porque task_reserve_slot devolve o ponteiro, mas o
// esquema de endereços de pilha é indexado por posição no array).
static int task_index_of(task_t* t) {
    return (int)(t - s_tasks);
}

void task_free_mapped_pages(address_space_t* as, uint64_t virt_start, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t va = virt_start + i * PAGE_SIZE_4K;
        uint64_t pa;
        if (paging_translate_in(as, va, &pa)) {
            paging_unmap_in(as, va);
            pfa_free_frame(pa);
        }
    }
}

task_t* task_reserve_slot(void) {
    // Reserva o slot de forma atômica em relação ao IRQ0: sem isso,
    // um scheduler_tick() no meio de find_free_slot() poderia levar
    // duas chamadas concorrentes (kernel thread + processo de
    // usuário sendo criados "ao mesmo tempo", do ponto de vista de
    // uma interrupção) a escolherem o mesmo slot livre.
    __asm__ volatile("cli");
    int slot_index;
    task_t* t = find_free_slot(&slot_index);
    if (t) t->state = TASK_BLOCKED; // reserva provisória: ainda não é
                                      // READY (não entra na fila do
                                      // scheduler ainda) nem UNUSED
                                      // (ninguém mais pode pegar este
                                      // slot enquanto ele é montado).
    __asm__ volatile("sti");
    return t;
}

bool task_alloc_kernel_stack_for(task_t* t,
                                  uint64_t* out_stack_top,
                                  uint64_t* out_stack_base,
                                  uint64_t* out_guard_addr) {
    int slot_index = task_index_of(t);

    // ---- reserva a faixa virtual deste slot e mapeia a pilha real ----
    // Layout do slot: [guard page (nunca mapeada)] [TASK_STACK_PAGES
    // páginas reais, mapeadas RW+NX] [1 página de respiro, nunca
    // mapeada]. A guard page fica logo ANTES da pilha real porque a
    // pilha cresce para endereços menores — um overflow "para baixo"
    // pisa primeiro nela, não no respiro do final.
    uint64_t slot_base   = stack_slot_base_addr(slot_index);
    uint64_t guard_addr  = slot_base;
    uint64_t stack_addr  = slot_base + PAGE_SIZE_4K;

    int mapped = 0;
    for (; mapped < TASK_STACK_PAGES; mapped++) {
        uint64_t phys = pfa_alloc_frame();
        if (phys == 0) break; // sem memória física suficiente

        uint64_t va = stack_addr + (uint64_t)mapped * PAGE_SIZE_4K;
        // NX: pilha de kernel nunca deveria ser executada — impede um
        // overflow que também controle o conteúdo da pilha de virar
        // execução de código arbitrário ali dentro.
        if (!paging_map(va, phys, PAGE_WRITABLE | PAGE_NO_EXEC)) {
            pfa_free_frame(phys); // a tabela intermediária não coube;
                                    // devolve o frame de dados também.
            break;
        }
    }
    if (mapped < TASK_STACK_PAGES) {
        task_free_mapped_pages(NULL, stack_addr, (uint64_t)mapped);
        return false;
    }

    *out_stack_top   = stack_addr + TASK_STACK_SIZE;
    *out_stack_base  = stack_addr;
    *out_guard_addr  = guard_addr;
    return true;
}

void task_publish_ready(task_t* t, const char* name) {
    // A partir daqui a task já deve estar com `rsp`/`stack_base`/
    // `guard_page_addr` (e, se for processo de usuário, os campos
    // user_*) preenchidos por quem chamou. Só falta tornar isso
    // visível ao scheduler de forma atômica em relação ao IRQ0 —
    // scheduler_add() mexe em t->next e no t->next da task atual;
    // se um scheduler_tick() caísse no meio disso, a fila circular
    // poderia ficar com um ponteiro inconsistente.
    __asm__ volatile("cli");
    t->id    = s_next_id++;
    t->next  = NULL; // scheduler_add() encaixa na fila
    t->state = TASK_READY;
    scheduler_add(t);
    __asm__ volatile("sti");

    if (name) {
        kstrncpy(t->name, name, TASK_NAME_MAX - 1);
        t->name[TASK_NAME_MAX - 1] = '\0';
    } else {
        t->name[0] = '\0';
    }
}

task_t* task_create(const char* name, task_entry_fn entry_point, void* arg) {
    task_t* t = task_reserve_slot();
    if (!t) return NULL; // tabela cheia

    uint64_t stack_top, stack_base, guard_addr;
    if (!task_alloc_kernel_stack_for(t, &stack_top, &stack_base, &guard_addr)) {
        t->state = TASK_UNUSED; // devolve o slot — sem isso ficaria
                                  // preso em TASK_BLOCKED para sempre.
        return NULL;
    }

    bootstrap_info_t* info = (bootstrap_info_t*)kmalloc(sizeof(bootstrap_info_t));
    if (!info) {
        task_free_mapped_pages(NULL, stack_base, TASK_STACK_PAGES);
        t->state = TASK_UNUSED;
        return NULL;
    }
    info->entry = entry_point;
    info->arg   = arg;

    // ---- monta a pilha inicial "como se" um task_switch já
    // tivesse rodado e empilhado o contexto de uma chamada para
    // task_bootstrap(info) ----
    //
    // Layout esperado por task_switch.asm ao dar POP (do topo da
    // pilha para baixo, ou seja, empilhamos na ordem inversa):
    //   RIP (endereço de retorno do "CALL" simulado) -> task_bootstrap_trampoline
    //   RBP, RBX, R12, R13, R14, R15  (nessa ordem de POP)
    //
    // task_bootstrap espera seu argumento em RDI (System V AMD64),
    // mas task_switch NÃO restaura RDI — ele só mexe em
    // RBX/RBP/R12-R15/RSP. Por isso guardamos `info` em R12 (um
    // registrador callee-saved que SOBREVIVE à troca) e usamos um
    // pequeno trampolim em ASM que move R12 -> RDI antes de chamar
    // task_bootstrap com a convenção C normal.
    uint64_t* sp = (uint64_t*)stack_top;

    extern void task_bootstrap_trampoline(void);
    *(--sp) = (uint64_t)task_bootstrap_trampoline;
    *(--sp) = 0; // RBP inicial (sem frame anterior)
    *(--sp) = 0; // RBX inicial
    *(--sp) = (uint64_t)info; // R12 — carrega o argumento para o trampolim
    *(--sp) = 0; // R13
    *(--sp) = 0; // R14
    *(--sp) = 0; // R15

    t->rsp               = (uint64_t)sp;
    t->stack_base        = stack_base;
    t->guard_page_addr   = guard_addr;
    t->is_user_task      = false;
    t->user_entry        = 0;
    t->user_stack_top    = 0;
    t->user_image_base   = 0;
    t->user_image_pages  = 0;
    t->user_stack_base   = 0;
    t->user_stack_pages  = 0;
    t->address_space     = NULL;
    t->heap_base         = 0;
    t->heap_current      = 0;
    t->heap_limit        = 0;
    for (int fd = 0; fd < TASK_MAX_OPEN_FILES; fd++) {
        t->fd_table[fd]  = NULL;
        t->fd_offset[fd] = 0;
    }
    t->stdout_owner      = NULL;
    t->stdout_head       = 0;
    t->stdout_tail       = 0;
    t->has_exited        = false;
    t->exit_code         = 0;

    task_publish_ready(t, name);
    return t;
}

_Noreturn void task_exit(void) {
    task_t* t = s_current;
    t->state = TASK_ZOMBIE;
    // A pilha de `t` (t->stack_base, e t->user_image_base/user_stack_base
    // se for processo de usuário) NÃO é liberada aqui de propósito:
    // ainda estamos executando NESSA MESMA pilha de kernel até o
    // próximo task_switch dentro de scheduler_yield() logo abaixo.
    // Liberar agora destruiria o chão debaixo dos nossos próprios pés.
    // A liberação de verdade acontece em task_reap_zombies(), chamado
    // de fora por outra task (tipicamente a task 0 / kernel_main),
    // depois que esta task já não está mais em execução.
    scheduler_yield(); // nunca retorna: a próxima task assume a CPU
    __builtin_unreachable();
}

// Percorre a tabela de tasks e informa se `addr` cai dentro da
// guard page reservada para alguma delas — usado pelo handler de
// #PF (isr.c) para diagnosticar overflow de pilha de kernel thread
// (ou de kernel dentro de um processo de usuário, já que todo
// processo também tem uma pilha de Ring 0) com uma mensagem
// específica em vez de um #PF genérico. Se encontrar, preenche
// out_task_name (buffer de pelo menos TASK_NAME_MAX bytes) e
// retorna true.
bool task_addr_is_guard_page(uint64_t addr, char* out_task_name) {
    for (int i = 0; i < TASK_MAX_COUNT; i++) {
        task_t* t = &s_tasks[i];
        if (t->state == TASK_UNUSED) continue;

        if (t->guard_page_addr != 0 &&
            addr >= t->guard_page_addr && addr < t->guard_page_addr + PAGE_SIZE_4K) {
            if (out_task_name) kstrncpy(out_task_name, t->name, TASK_NAME_MAX - 1);
            return true;
        }

        // Zona entre o fim da pilha de USUÁRIO e o início da heap
        // (ver USER_STACK_GAP_PAGES em process.c) — deliberadamente
        // nunca mapeada, funciona como guard page mesmo sem ser
        // criada por task_alloc_kernel_stack_for. Um overflow de
        // pilha de usuário (ex.: recursão funda, ou muitas chamadas
        // de printf/malloc aninhadas antes de a pilha ser grande o
        // suficiente) cai aqui e agora é diagnosticado explicitamente
        // em vez de corromper silenciosamente o início da heap, que
        // ficava logo depois.
        if (t->is_user_task && t->user_stack_base != 0 && t->heap_base != 0) {
            uint64_t stack_end = t->user_stack_base + t->user_stack_pages * PAGE_SIZE_4K;
            if (addr >= stack_end && addr < t->heap_base) {
                if (out_task_name) kstrncpy(out_task_name, t->name, TASK_NAME_MAX - 1);
                return true;
            }
        }
    }
    return false;
}

bool task_any_user_process_alive(void) {
    for (int i = 0; i < TASK_MAX_COUNT; i++) {
        task_t* t = &s_tasks[i];
        if (t->state == TASK_UNUSED) continue;
        if (t->is_user_task) return true;
    }
    return false;
}

int task_reap_zombies(void) {
    int reaped = 0;
    task_t* self = s_current;

    // Desabilita interrupções durante toda a varredura+liberação: um
    // scheduler_tick() disparado pelo PIT no meio disto poderia trocar
    // para outra task e voltar bem depois, com a tabela de tasks e as
    // tabelas de página em estado parcialmente modificado — não temos
    // spinlocks ainda nesta fase, então "cli/sti" ao redor da seção
    // crítica é a proteção disponível. A janela é curta (poucas
    // páginas por task zumbi), então o impacto na latência de
    // interrupção é desprezível.
    __asm__ volatile("cli");

    for (int i = 0; i < TASK_MAX_COUNT; i++) {
        task_t* t = &s_tasks[i];
        if (t->state != TASK_ZOMBIE) continue;
        if (t == self) continue; // salvaguarda: nunca liberar a própria
                                   // pilha em que estamos executando
                                   // agora, mesmo que por algum engano
                                   // ela esteja marcada como zumbi.
        if (t->is_user_task && t->stdout_owner != NULL) {
            // Ainda há um terminal "dono" deste processo que pode não
            // ter tido a chance de ler has_exited/exit_code e mostrar
            // a mensagem final ainda (term_update_running_process, em
            // terminal.cpp, só roda a cada frame de draw() daquele
            // terminal — mais frequente que o reaper, mas não
            // simultâneo). Adiar a coleta até o terminal soltar essa
            // referência (o que ele faz assim que processa
            // has_exited, ou quando a janela é fechada) evita a
            // corrida onde o reaper colhe a task exatamente no
            // intervalo entre o sys_exit e o próximo draw() do
            // terminal, fazendo a mensagem final nunca aparecer.
            continue;
        }

        if (t->stack_base != 0) {
            task_free_mapped_pages(NULL, t->stack_base, TASK_STACK_PAGES);
        }
        // Processo de usuário: também libera a imagem do executável
        // (código+dados) e a pilha de USUÁRIO — essas vivem no
        // espaço de endereço ISOLADO do processo (Fase 3), não no do
        // kernel, então passamos t->address_space explicitamente em
        // vez de NULL (que significaria "o espaço ativo agora" — a
        // task 0/GUI rodando o reaper quase certamente não está com
        // o espaço deste processo ativo no momento).
        if (t->is_user_task) {
            if (t->user_image_base != 0 && t->user_image_pages != 0) {
                task_free_mapped_pages(t->address_space, t->user_image_base, t->user_image_pages);
            }
            if (t->user_stack_base != 0 && t->user_stack_pages != 0) {
                task_free_mapped_pages(t->address_space, t->user_stack_base, t->user_stack_pages);
            }
            // Libera a heap de usuário (sbrk) — só as páginas que
            // chegaram a ser mapeadas de fato (heap_current pode não
            // ter avançado tudo até heap_limit; liberar além de
            // heap_current tentaria desmapear páginas que nunca
            // existiram, o que task_free_mapped_pages já trata com
            // segurança via paging_translate_in falhando, mas é mais
            // claro já calcular a contagem certa aqui).
            if (t->heap_base != 0 && t->heap_current > t->heap_base) {
                uint64_t heap_pages = (t->heap_current - t->heap_base) / PAGE_SIZE_4K;
                task_free_mapped_pages(t->address_space, t->heap_base, heap_pages);
            }
            // Fecha quaisquer descritores de arquivo ainda abertos —
            // um processo que termina sem chamar close() explicitamente
            // (crash, ou simplesmente esqueceu) não deveria vazar essa
            // referência para sempre. VfsNode não tem um "contador de
            // referências" nesta fase, então "fechar" aqui é só
            // remover a entrada da tabela deste processo.
            for (int fd = 0; fd < TASK_MAX_OPEN_FILES; fd++) {
                t->fd_table[fd]  = NULL;
                t->fd_offset[fd] = 0;
            }
            // Destrói o espaço de endereço isolado deste processo em
            // si (as tabelas PDPT/PD/PT da metade de usuário do PML4
            // dele) — precisa acontecer DEPOIS das liberações acima,
            // que ainda precisam das tabelas existirem para
            // traduzir/desmapear.
            if (t->address_space) {
                paging_destroy_address_space(t->address_space);
                t->address_space = NULL;
            }
        }

        // Remove `t` da fila circular do scheduler ANTES de liberar o
        // slot para reuso — ver comentário extenso em scheduler.c
        // (scheduler_remove) sobre o bug de self-loop que acontecia
        // sem isso: sem remover, o próximo task_create()/
        // process_create() que reciclasse este MESMO slot chamaria
        // scheduler_add() de novo sobre uma struct cujo ponteiro
        // .next antigo ainda apontava para dentro da fila —
        // corrompendo a fila circular de forma silenciosa, sem
        // nenhuma exceção de CPU, e travando o sistema na primeira
        // vez que essa task reciclada tentasse ceder a CPU de volta.
        scheduler_remove(t);

        t->stack_base        = 0;
        t->guard_page_addr   = 0;
        t->rsp               = 0;
        t->is_user_task      = false;
        t->user_entry        = 0;
        t->user_stack_top    = 0;
        t->user_image_base   = 0;
        t->user_image_pages  = 0;
        t->user_stack_base   = 0;
        t->user_stack_pages  = 0;
        t->address_space     = NULL; // já destruído acima se era um processo
        t->heap_base         = 0;
        t->heap_current      = 0;
        t->heap_limit        = 0;
        t->stdout_owner      = NULL;
        t->stdout_head       = 0;
        t->stdout_tail       = 0;
        t->has_exited        = false;
        t->exit_code         = 0;
        t->state             = TASK_UNUSED; // slot pronto para reuso
        reaped++;
    }

    __asm__ volatile("sti");
    return reaped;
}

// ---- Canal de saída (stdout) de processos de usuário ----
//
// Buffer circular clássico: stdout_head é onde o PRODUTOR (sys_write,
// chamado a partir do processo de usuário via syscall) escreve o
// próximo byte; stdout_tail é onde o CONSUMIDOR (o terminal, lendo a
// partir da task 0) lê o próximo byte ainda não consumido. "Cheio"
// aqui é definido como head alcançando tail por trás — nesse caso,
// escolhemos SOBRESCREVER o byte mais antigo ainda não lido
// (avançando tail junto), em vez de bloquear o produtor: um processo
// de usuário não tem como "dormir" nesta fase (não há syscall de
// espera), então bloquear a escrita travaria a task inteira até o
// terminal ler — pior do que perder alguns bytes de log antigos.
#define TASK_STDOUT_RING_SIZE (sizeof(((task_t*)0)->stdout_ring))

uint64_t task_stdout_write(task_t* t, const uint8_t* data, uint64_t len) {
    if (!t || !data) return 0;
    for (uint64_t i = 0; i < len; i++) {
        uint16_t next_head = (uint16_t)((t->stdout_head + 1) % TASK_STDOUT_RING_SIZE);
        if (next_head == t->stdout_tail) {
            // Buffer cheio: descarta o byte mais antigo avançando tail,
            // para abrir espaço ao novo byte (política "sobrescreve o
            // mais antigo", não "descarta o mais novo" — mantém a
            // saída mais RECENTE visível, que costuma ser mais
            // relevante para depuração do que o início de uma saída
            // longa que o terminal ainda não teve chance de ler).
            t->stdout_tail = (uint16_t)((t->stdout_tail + 1) % TASK_STDOUT_RING_SIZE);
        }
        t->stdout_ring[t->stdout_head] = data[i];
        t->stdout_head = next_head;
    }
    return len;
}

uint64_t task_stdout_read(task_t* t, uint8_t* out_buf, uint64_t max_len) {
    if (!t || !out_buf) return 0;
    uint64_t n = 0;
    while (n < max_len && t->stdout_tail != t->stdout_head) {
        out_buf[n++] = t->stdout_ring[t->stdout_tail];
        t->stdout_tail = (uint16_t)((t->stdout_tail + 1) % TASK_STDOUT_RING_SIZE);
    }
    return n;
}
