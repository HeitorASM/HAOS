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
align 16
; Guarda temporariamente o RSP de usuário enquanto usamos a stack
; de kernel — precisa ser restaurado antes do SYSRET.
user_rsp_scratch: resq 1

; Stack de kernel dedicada para o corpo da syscall (separada da
; stack de kernel usada por interrupções via TSS.RSP0, para não
; haver conflito caso uma IRQ ocorra durante o processamento).
SYSCALL_STACK_SIZE equ 16384
syscall_stack: resb SYSCALL_STACK_SIZE

section .text

global syscall_entry
syscall_entry:
    ; --- Troca imediata para stack de kernel ---
    mov [rel user_rsp_scratch], rsp
    lea rsp, [rel syscall_stack + SYSCALL_STACK_SIZE]

    ; --- Preserva RCX (RIP de retorno) e R11 (RFLAGS) até o SYSRET final ---
    push rcx
    push r11

    ; --- Reorganiza argumentos para a convenção de chamada C
    ;     (ver explicação do "shift em cadeia" no cabeçalho acima) ---
    mov r9,  r8      ; a5: r8  -> r9
    mov r8,  r10      ; a4: r10 -> r8
    mov rcx, rdx      ; a3: rdx -> rcx
    mov rdx, rsi      ; a2: rsi -> rdx
    mov rsi, rdi      ; a1: rdi -> rsi
    mov rdi, rax      ; num: rax -> rdi

    call syscall_dispatch   ; retorno em rax, pronto para devolver ao usuário

    ; --- Restaura RCX/R11 exigidos pelo SYSRET ---
    pop r11
    pop rcx

    ; --- Restaura a stack de usuário ---
    mov rsp, [rel user_rsp_scratch]

    o64 sysret        ; RIP<-RCX, RFLAGS<-R11, CS/SS<-STAR (volta para Ring 3)

section .note.GNU-stack noalloc noexec nowrite progbits
