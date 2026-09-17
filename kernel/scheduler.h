#pragma once
#include "types.h"
#include "task.h"

// ============================================================
//  kernel/scheduler.h — Escalonador Round-Robin (etapa 1)
//
//  Política: fila circular simples. A cada N ticks do PIT
//  (SCHEDULER_QUANTUM_TICKS), a task atual volta para o fim da
//  fila (se ainda estiver READY/RUNNING) e a próxima da fila
//  assume a CPU. Sem prioridades ainda — todas as tasks têm o
//  mesmo peso. Isso é deliberado: primeiro provar que o
//  context switch e o tick-based preemption funcionam de ponta a
//  ponta, prioridade e nice values entram depois com menos risco.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Quantos ticks do PIT (a 100Hz => 10ms cada) cada task roda antes
// de ceder a CPU. 5 ticks = ~50ms de fatia de tempo.
#define SCHEDULER_QUANTUM_TICKS  5

// Inicializa a fila do scheduler com a task 0 (kernel_main) como
// única entrada, em estado RUNNING. Deve ser chamado logo após
// tasking_init().
void scheduler_init(void);

// Insere uma task já criada (TASK_READY) no fim da fila circular.
void scheduler_add(task_t* t);

// Remove `t` da fila circular do scheduler, religando os vizinhos
// dela para pularem-na. OBRIGATÓRIO chamar isto antes de reciclar o
// slot de uma task para um novo task_create()/process_create() —
// sem isso, a fila fica com uma entrada "fantasma" apontando para
// uma struct que vai ser reaproveitada, e a próxima inserção nesse
// mesmo slot pode corromper a fila circular inteira (self-loop —
// ver o comentário extenso na implementação, em scheduler.c, para o
// caso concreto que isso causava). Chamado por task_reap_zombies()
// em task.c.
void scheduler_remove(task_t* t);

// Chamado pelo handler de IRQ0 (timer) a CADA tick, não só quando
// o quantum expira — internamente decide se é hora de trocar de
// task. Se decidir trocar, chama task_switch() (ASM) diretamente;
// portanto scheduler_tick() PODE fazer o fluxo de execução "sumir"
// daqui e só "reaparecer" na próxima vez que esta mesma task for
// escalonada de novo. Deve ser chamado com interrupções mascaradas
// (dentro do próprio handler de IRQ, que já roda com IF=0 na
// entrada de uma interrupt gate).
void scheduler_tick(void);

// Força uma troca de contexto imediatamente, sem esperar o
// quantum expirar (ex.: usado por task_exit() e, futuramente, por
// syscalls bloqueantes / task_yield() explícito).
void scheduler_yield(void);

#ifdef __cplusplus
}
#endif
