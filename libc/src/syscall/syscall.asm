; libc/src/syscall/syscall.asm — haos_syscall(num, a1, a2, a3, a4, a5)
;
; Convenção System V AMD64 padrão de chamada de função (como o C vê
; esta função):
;   rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=a4, r9=a5
;
; Convenção da instrução SYSCALL, que o kernel espera (ver
; kernel/syscall.h):
;   rax=num, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5
;
; Este wrapper só reorganiza os registradores entre as duas
; convenções. A ordem dos MOVs importa: cada registrador de origem
; só pode ser lido ANTES de ser sobrescrito como destino de um mov
; anterior — a ordem abaixo foi verificada (por simulação) para não
; ter nenhum hazard de "li o valor errado porque já tinha sido
; sobrescrito". Em particular, "mov r10, r8" (copia a4) precisa
; acontecer ANTES de "mov r8, r9" (que sobrescreveria r8 com a5).
;
; RCX é destruído pela própria instrução SYSCALL (guarda o RIP de
; retorno) — por isso lemos rcx (a3) para dentro de rdx ANTES de
; emitir a instrução syscall; depois disso rcx já não tem mais o
; valor original, mas nesse ponto já não precisamos mais dele.
bits 64

section .text
global haos_syscall

haos_syscall:
    mov rax, rdi        ; num -> rax
    mov rdi, rsi        ; a1  -> rdi
    mov rsi, rdx        ; a2  -> rsi
    mov rdx, rcx        ; a3  -> rdx (lido de rcx ANTES do syscall destruí-lo)
    mov r10, r8         ; a4  -> r10 (lido de r8 ANTES de r8 ser reescrito abaixo)
    mov r8,  r9         ; a5  -> r8
    syscall
    ret                 ; valor de retorno já está em rax, exatamente
                         ; onde o C espera o retorno da função

section .note.GNU-stack noalloc noexec nowrite progbits
