; libc/src/crt/crt0.asm — ponto de entrada real de todo programa HEXEC
;
; O loader do kernel (process.c) salta para o offset "entry_offset"
; do header HEXEC (ver kernel/hexec.h) — é ESTE símbolo, _start, que
; deve estar nesse offset (o linker script da libc, ver
; libc/hexec.ld, garante isso ao colocar crt0.o no início do .text).
;
; Responsabilidades de _start:
;   1. Ler argc/argv do TOPO da pilha de usuário — o loader
;      (process_create, em kernel/process.c) monta esses dados lá
;      ANTES de saltar para cá, seguindo a mesma convenção usada pelo
;      _start do Linux x86_64: [RSP] = argc, [RSP+8] = argv[0],
;      [RSP+16] = argv[1], ..., terminado por um ponteiro NULL. Não
;      inventamos uma convenção nova de propósito — reaproveitar uma
;      já conhecida reduz a chance de erro e facilita comparar com
;      referências existentes se algo não bater.
;   2. Zerar RBP — sinaliza para qualquer stack unwinder (não que o
;      HAOS tenha um hoje) que esta é a base da pilha, sem frame
;      anterior. Também é higiene padrão de todo _start de C runtime.
;   3. Chamar main(argc, argv) — ATENÇÃO: main() PRECISA aceitar
;      (int argc, char** argv) agora, não mais main(void). Um
;      programa que ainda declara main(void) continua funcionando
;      (a ABI x86_64 não reclama de argumentos extras não lidos),
;      mas não terá acesso a argc/argv sem atualizar sua assinatura.
;   4. Chamar exit() com o valor de retorno de main() — assim um
;      programa pode simplesmente terminar com "return 0;" em vez de
;      ser obrigado a chamar exit()/_exit() manualmente, igual em
;      qualquer C hospedado normal.
bits 64

section .text
global _start
extern main
extern exit

_start:
    ; Neste ponto (recém-saltado do loader, ANTES de qualquer push
    ; nosso), a pilha está exatamente como process_create() a
    ; montou:
    ;   [rsp+0]  = argc
    ;   [rsp+8]  = argv[0]
    ;   [rsp+16] = argv[1]
    ;   ...
    ;   [rsp+8*argc] = NULL (terminador de argv)
    pop rdi              ; argc -> primeiro argumento de main (RDI)
    mov rsi, rsp         ; argv = endereço atual do topo da pilha,
                          ; que agora (depois do pop acima) já aponta
                          ; exatamente para argv[0] -> segundo
                          ; argumento de main (RSI)

    xor rbp, rbp          ; RBP=0: marca "sem frame anterior"
    call main             ; main(argc, argv) -> retorno em EAX
    movsx rdi, eax        ; sign-extend de 32 para 64 bits para exit()
    call exit             ; nunca retorna

section .note.GNU-stack noalloc noexec nowrite progbits
