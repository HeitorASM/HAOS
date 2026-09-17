; libc/src/crt/crt0.asm — ponto de entrada real de todo programa HEXEC
;
; O loader do kernel (process.c) salta para o offset "entry_offset"
; do header HEXEC (ver kernel/hexec.h) — é ESTE símbolo, _start, que
; deve estar nesse offset (o linker script da libc, ver
; libc/hexec.ld, garante isso ao colocar crt0.o no início do .text).
;
; Responsabilidades de _start:
;   1. Zerar RBP — sinaliza para qualquer stack unwinder (não que o
;      HAOS tenha um hoje) que esta é a base da pilha, sem frame
;      anterior. Também é higiene padrão de todo _start de C runtime.
;   2. Chamar main() — hoje sem argumentos (void), porque o loader
;      HEXEC não passa argc/argv nem envp ainda; isso é uma limitação
;      conhecida a resolver quando o formato de executável evoluir
;      (ver hexec.h) para incluir uma forma de passar parâmetros de
;      linha de comando.
;   3. Chamar exit() com o valor de retorno de main() — assim um
;      programa pode simplesmente terminar com "return 0;" em vez de
;      ser obrigado a chamar exit()/_exit() manualmente, igual em
;      qualquer C hospedado normal.
;
; RSP na entrada: o loader (process.c) já posiciona a pilha de
; usuário corretamente antes de saltar para cá (ver
; PROCESS_USER_STACK_PAGES em process.h) — não há argc/argv
; empilhados nela ainda (ver limitação acima), então nenhuma leitura
; de pilha é necessária aqui além do que main()/exit() já fazem
; naturalmente via ABI normal de chamada de função.
bits 64

section .text
global _start
extern main
extern exit

_start:
    xor rbp, rbp      ; RBP=0: marca "sem frame anterior" para quem
                       ; algum dia quiser fazer stack unwinding
    call main          ; main() -> valor de retorno em EAX (convenção
                       ; padrão de retorno de int em System V AMD64)
    movsx rdi, eax     ; exit(int status) espera o argumento em RDI;
                       ; sign-extend de 32 para 64 bits, igual ao que
                       ; qualquer libc hospedada faz aqui.
    call exit          ; nunca retorna

section .note.GNU-stack noalloc noexec nowrite progbits
