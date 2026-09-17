; kernel/task_switch.asm — troca de contexto entre kernel threads
bits 64

section .text
global task_switch

; ============================================================
;  void task_switch(uint64_t* old_rsp_ptr, uint64_t new_rsp);
;
;  RDI = endereço de onde salvar o RSP atual (task->rsp da task
;        que está SAINDO da CPU — passamos o ENDEREÇO do campo,
;        não o valor, porque quem chama não tem como saber o
;        valor final do RSP antes de nós empilharmos o contexto)
;  RSI = valor de RSP a restaurar (task->rsp da task que vai
;        ENTRAR na CPU — lido pelo scheduler ANTES de chamar isto)
;
;  Convenção System V AMD64: o CALLEE (esta função) só é obrigado
;  a preservar RBX, RBP, R12-R15 (e RSP, obviamente). Todos os
;  outros registradores já foram salvos pelo CALLER se precisava
;  deles — por isso só empilhamos esses 6 aqui. RIP de retorno já
;  está no topo da pilha por causa do CALL que trouxe a execução
;  até aqui; não precisamos empilhá-lo manualmente.
;
;  Fluxo:
;    1. Empilha o contexto da task atual (RIP já está lá via CALL).
;    2. Salva o RSP resultante em *old_rsp_ptr.
;    3. Troca RSP para new_rsp (pilha da PRÓXIMA task).
;    4. Desempilha o contexto dessa pilha — que é EXATAMENTE o
;       mesmo layout, porque toda task só entra na CPU através
;       desta mesma função (seja retomando de um switch anterior,
;       seja pela primeira vez, com uma pilha inicial montada à
;       mão por task_create() para simular esse mesmo layout).
;    5. RET — mas o RIP que estava empilhado na pilha da NOVA task
;       não aponta necessariamente para o meio de task_switch: se
;       é a primeira vez que essa task roda, RIP aponta para
;       task_bootstrap (ver task.c), que nunca vai dar RET de
;       volta para cá — só chama entry_point(arg) e depois
;       task_exit(). Se é uma task retomando, RIP aponta de volta
;       para logo depois do CALL task_switch anterior, dentro do
;       corpo de scheduler_tick/scheduler_yield.
; ============================================================
task_switch:
    ; ---- salva contexto da task que está saindo ----
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov  [rdi], rsp      ; *old_rsp_ptr = RSP atual (com o contexto já empilhado)

    ; ---- troca de pilha ----
    mov  rsp, rsi        ; RSP = new_rsp (pilha da próxima task)

    ; ---- restaura contexto da task que está entrando ----
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rbx
    pop  rbp

    ; ---- BUG CRÍTICO CORRIGIDO AQUI: garante IF=1 na task que entra ----
    ; A instrução SYSCALL desabilita interrupções automaticamente na
    ; entrada (via SFMASK, ver syscall_init em syscall.c) e NADA as
    ; reabilita explicitamente ao longo do caminho sys_exit() ->
    ; task_exit() -> scheduler_yield() -> task_switch(). Antes desta
    ; correção, quando um processo de usuário chamava sys_exit(), a
    ; task escolhida para assumir a CPU herdava esse estado e passava
    ; a rodar CEGA A QUALQUER INTERRUPÇÃO — o PIT nunca mais disparava
    ; (então o scheduler nunca mais trocava de task automaticamente) e
    ; teclado/mouse nunca mais eram processados. O sistema continuava
    ; "vivo" (a CPU seguia executando o loop da GUI normalmente) mas
    ; congelado para qualquer interação, sem nenhuma exceção de CPU
    ; disparar — exatamente o "trava 100%, nem kernel panic" relatado.
    ;
    ; A correção: toda vez que QUALQUER task assume a CPU através
    ; deste ponto, garantimos IF=1 incondicionalmente. Isso é seguro
    ; também para trocas vindas de dentro do handler de IRQ0
    ; (scheduler_tick): quando isso acontece, o EOI do PIC já foi
    ; enviado ANTES da chamada a scheduler_tick() (ver irq0_handler em
    ; idt_asm.asm), e a task que assume a CPU "sai" de dentro do
    ; handler original sem nunca completar o IRETQ dele — então não há
    ; frame de interrupção pendente que um "sti" precoce corromperia.
    sti

    ret                  ; "retorna" para o RIP empilhado nessa pilha —
                          ; que pode ser task_bootstrap_trampoline
                          ; (primeira vez) ou o ponto de retorno de um
                          ; switch anterior.

; ============================================================
;  task_bootstrap_trampoline
;
;  Alcançado via RET (não CALL) na primeira vez que uma task nova
;  entra na CPU. Nesse momento R12 contém o ponteiro `info*`
;  (bootstrap_info_t*) que task_create() colocou lá manualmente ao
;  montar a pilha inicial — é o único jeito de "passar um
;  argumento" para o primeiro código de uma task nova, já que
;  task_switch não mexe em RDI/RSI (não são callee-saved) e RIP é
;  o único outro dado que atravessa a troca.
;
;  Este trampolim só existe para mover esse ponteiro de R12 (onde
;  sobrevive à troca de contexto) para RDI (onde a ABI System V
;  espera o primeiro argumento de uma função C normal), e então
;  chamar task_bootstrap de verdade com a assinatura C limpa.
; ============================================================
extern task_bootstrap
global task_bootstrap_trampoline
task_bootstrap_trampoline:
    mov  rdi, r12
    call task_bootstrap
    ; task_bootstrap nunca retorna (termina chamando task_exit,
    ; que por sua vez nunca retorna) — não há RET aqui de propósito.

; ============================================================
;  user_bootstrap_trampoline
;
;  Equivalente a task_bootstrap_trampoline, mas para PROCESSOS DE
;  USUÁRIO (Fase 2, ver process.c). Diferença: process_bootstrap()
;  não recebe nenhum argumento por registrador — ela mesma chama
;  task_current() para descobrir tudo que precisa (user_entry,
;  user_stack_top). Por isso este trampolim não precisa mover nada
;  de R12 para RDI; só existe para dar a process_bootstrap() um
;  ponto de entrada alcançável via RET (mesma técnica de
;  "simular uma chamada de função" usada em toda a pilha inicial
;  montada por process_create()).
extern process_bootstrap
global user_bootstrap_trampoline
user_bootstrap_trampoline:
    call process_bootstrap
    ; process_bootstrap nunca retorna (termina em enter_usermode,
    ; que faz IRETQ para Ring 3 e nunca volta por RET) — não há RET
    ; aqui de propósito.

section .note.GNU-stack noalloc noexec nowrite progbits
