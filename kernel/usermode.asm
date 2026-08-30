; ============================================================
;
;  A única forma "oficial" de baixar o CPL (privilege level) em
;  x86_64 sem usar SYSRET é através de um IRETQ montado manualmente:
;  empilhamos os mesmos 5 valores que a CPU empilharia sozinha ao
;  entrar numa interrupção (SS, RSP, RFLAGS, CS, RIP), só que com
;  os seletores de USUÁRIO em vez dos de kernel, e IRETQ interpreta
;  isso como "essa interrupção veio do Ring 3" — trocando o CPL de
;  volta para 3 e recarregando os registradores de segmento.
;
;  RCX aqui é só uma variável local nossa (não confundir com o RCX
;  usado por SYSCALL/SYSRET — isso é IRETQ, mecanismo diferente).
; ============================================================
bits 64

extern tss_set_kernel_stack   ; disponível para o C chamar depois; não usado aqui diretamente

global enter_usermode
; System V ABI: rdi = entry_point, rsi = user_stack_top
enter_usermode:
    cli                        ; ninguém deve interromper a montagem do frame

    mov ax, 0x18 | 3            ; GDT_USER_DATA com RPL=3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; SS é recarregado pelo próprio IRETQ a partir do frame abaixo,
    ; não precisa (e não deve) ser setado manualmente aqui.

    ; ---- Monta o frame de IRETQ (ordem que a CPU espera, de cima
    ;      para baixo na pilha): SS, RSP, RFLAGS, CS, RIP ----
    push qword (0x18 | 3)       ; SS  = user data selector, RPL=3
    push rsi                    ; RSP = topo da pilha de usuário
    pushfq                      ; RFLAGS atual...
    pop  rax
    or   rax, 0x200              ; garante IF=1 (interrupções habilitadas em Ring 3)
    push rax                    ; ...empilhado de volta como RFLAGS do frame
    push qword (0x20 | 3)       ; CS  = user code selector, RPL=3
    push rdi                    ; RIP = entry_point

    iretq                       ; salta para Ring 3 — nunca retorna daqui

section .note.GNU-stack noalloc noexec nowrite progbits
