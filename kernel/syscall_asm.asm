; ============================================================
;  Entrada/saída de SYSCALL
;
;  Diferente de uma interrupção normal (onde TSS.RSP0 troca a
;  stack automaticamente via hardware), a instrução SYSCALL NÃO
;  troca RSP. Ao entrar aqui, RSP ainda é a stack de USUÁRIO —
;  então a primeiríssima coisa que fazemos é trocar manualmente
;  para uma stack de kernel, antes de tocar em qualquer coisa que
;  possa gerar uma exceção (senão a CPU tentaria empilhar o frame
;  de exceção na stack de usuário, que pode nem estar mapeada
;  para escrita de kernel).
;
;  O que SYSCALL faz sozinho (hardware):
;    RCX  <- RIP de retorno (endereço da instrução após 'syscall')
;    R11  <- RFLAGS antes da chamada
;    CS   <- STAR[47:32]         (kernel code, forçado para DPL0)
;    SS   <- STAR[47:32] + 8     (kernel data)
;    RFLAGS <- RFLAGS & ~SFMASK  (mascara IF, entre outras)
;    RIP  <- LSTAR                (aqui: syscall_entry)
;
;  O que SYSRET faz sozinho (hardware):
;    RIP  <- RCX
;    RFLAGS <- R11
;    CS   <- STAR[63:48] + 16     (user code, DPL3)
;    SS   <- STAR[63:48] + 8      (user data, DPL3)
;
;  Por isso RCX e R11 são "sagrados": guardamos na stack assim que
;  entramos e só restauramos logo antes do SYSRET final.
;
;  Convenção de entrada (syscall): rax=num, rdi,rsi,rdx,r10,r8=args 1-5
;  (usa r10 no lugar de rcx, já que rcx guarda o RIP de retorno).
;
;  Convertemos para a convenção de chamada C (System V):
;  syscall_dispatch(num, a1, a2, a3, a4, a5) -> rdi,rsi,rdx,rcx,r8,r9
;
;  Isso é um "shift em cadeia": cada registrador de entrada vira o
;  próximo da convenção C. Processar em ORDEM REVERSA (do último elo
;  da cadeia para o primeiro) garante que nenhum mov sobrescreve uma
;  fonte antes dela ser lida:
;
;    r8  -> r9   (a5)
;    r10 -> r8   (a4)
;    rdx -> rcx  (a3)
;    rsi -> rdx  (a2)
;    rdi -> rsi  (a1)
;    rax -> rdi  (num)
; ============================================================
bits 64

extern syscall_dispatch    ; uint64_t syscall_dispatch(num, a1, a2, a3, a4, a5)

section .bss
; ---- Topo da pilha de kernel a usar PARA ESTA SYSCALL ----
;
; Era, antes desta correção, um buffer fixo (`syscall_stack`)
; compartilhado por TODA E QUALQUER task que entrasse aqui — o que
; era seguro enquanto só existia UM processo de usuário em Ring 3 de
; cada vez (o modelo do projeto até a introdução de spawn()+waitpid()).
; Com waitpid() bloqueando o PAI no MEIO de uma syscall (dentro do
; próprio corpo de sys_waitpid, via scheduler_yield()) enquanto um
; FILHO entra em Ring 3 e faz suas próprias syscalls, dois processos
; de usuário passavam a usar a mesma `syscall_stack` ao mesmo tempo —
; reentrância clássica, cada um sobrescrevendo o que o outro tinha
; empilhado. Sintoma observado: o sistema travava (ou corrompia
; silenciosamente) ao primeiro waitpid() real num processo com
; filhos concorrentes (ver examples/launcher).
;
; Correção: cada processo de usuário já tem sua PRÓPRIA pilha de
; kernel dedicada (task_t::stack_base, TASK_STACK_SIZE = 16KB — a
; mesma usada como TSS.RSP0 para interrupções em Ring 3, ver gdt.c e
; scheduler.c/do_switch_to). Em vez de uma pilha global, usamos essa
; pilha por-task também para o corpo da syscall: current_syscall_stack_top
; é atualizada em C por scheduler_set_syscall_stack() (chamada de
; dentro de do_switch_to, scheduler.c, no mesmo lugar que já atualiza
; TSS.RSP0) toda vez que uma task assume a CPU. Isso é seguro mesmo
; com uma IRQ interrompendo uma syscall em andamento: a IRQ empilha
; por CIMA do que a syscall já tinha empilhado nessa mesma pilha —
; exatamente como pilhas de kernel aninhadas sempre funcionam —, e
; nunca há DOIS processos usando o mesmo endereço de pilha ao mesmo
; tempo, porque cada um tem a sua.
;
; Inicializada em 0 até a primeira troca de contexto configurá-la;
; isso é seguro porque task 0 (kernel_main/GUI) nunca executa em
; Ring 3 e portanto nunca chega em syscall_entry — o primeiro uso
; real desta variável só acontece depois que algum processo de
; usuário já rodou pelo menos uma vez, momento em que
; scheduler_set_syscall_stack() já terá sido chamada para ele.
current_syscall_stack_top: resq 1

section .text

; void set_current_syscall_stack(uint64_t top);  (RDI = top)
;
; Função C-ABI simples para gravar current_syscall_stack_top a partir
; de código C — seguindo o mesmo padrão já usado por
; tss_set_kernel_stack() (gdt.c) para o campo equivalente da TSS.
; Chamada por scheduler_set_syscall_stack() (scheduler.c, dentro de
; do_switch_to, no mesmo lugar que já chama tss_set_kernel_stack) e,
; uma única vez, por kernel_main (kernel.c) na inicialização, com um
; valor seguro de fallback — ver comentário grande acima de
; current_syscall_stack_top sobre por quê.
global set_current_syscall_stack
set_current_syscall_stack:
    mov [rel current_syscall_stack_top], rdi
    ret

global syscall_entry
syscall_entry:
    ; --- Troca imediata para stack de kernel DESTA task ---
    mov r9, rsp
    mov rsp, [rel current_syscall_stack_top]
    push r9

    ; --- Preserva RCX (RIP de retorno) e R11 (RFLAGS) até o SYSRET final ---
    push rcx
    push r11
    sub rsp, 8

    ; --- Reorganiza argumentos para a convenção de chamada C
    ;     (ver explicação do "shift em cadeia" no cabeçalho acima) ---
    mov r9,  r8      ; a5: r8  -> r9
    mov r8,  r10      ; a4: r10 -> r8
    mov rcx, rdx      ; a3: rdx -> rcx
    mov rdx, rsi      ; a2: rsi -> rdx
    mov rsi, rdi      ; a1: rdi -> rsi
    mov rdi, rax      ; num: rax -> rdi

    call syscall_dispatch   ; retorno em rax, pronto para devolver ao usuário
    add rsp, 8

    ; --- Restaura RCX/R11 exigidos pelo SYSRET ---
    pop r11
    pop rcx
    pop r9

    ; --- Restaura a stack de usuário ---
    mov rsp, r9

    o64 sysret        ; RIP<-RCX, RFLAGS<-R11, CS/SS<-STAR (volta para Ring 3)

section .note.GNU-stack noalloc noexec nowrite progbits
