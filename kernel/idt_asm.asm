; kernel/idt_asm.asm — handlers de IRQ em assembly 64-bit
bits 64

; ---- Exporta variáveis globais -----------------------------------
global timer_ticks
global keyboard_buf
global keyboard_hd
global keyboard_tl
global mouse_buf
global mouse_hd
global mouse_tl

section .bss
timer_ticks:  resq 1      ; contador de ticks do timer (100Hz)

; Ring buffer para scancodes do teclado (32 slots)
keyboard_buf: resb 32
keyboard_hd:  resb 1      ; head (escrito pela ISR)
keyboard_tl:  resb 1      ; tail (lido pelo C)

; Ring buffer para bytes crus do mouse (32 slots)
mouse_buf:    resb 32
mouse_hd:     resb 1      ; head (escrito pela ISR)
mouse_tl:     resb 1      ; tail (lido pelo C)

section .text
global irq0_handler
global irq1_handler
global irq12_handler
global default_handler

; ---- Timer IRQ0 (INT 32) -----------------------------------------
;
; Agora chama scheduler_tick() em C a cada tick. Isso exige salvar
; TODOS os registradores caller-saved antes da chamada (a função C
; pode livremente usar RAX/RCX/RDX/RSI/RDI/R8-R11 sem avisar) — os
; callee-saved (RBX/RBP/R12-R15) são preservados pela própria
; convenção de chamada dentro de scheduler_tick, então não
; precisamos empilhá-los aqui.
;
; scheduler_tick() PODE chamar task_switch() internamente, o que
; faz esta mesma execução "sumir" daqui até a próxima vez que esta
; task for escalonada — quando isso acontece, ela retoma
; exatamente no PONTO SEGUINTE à chamada de scheduler_tick, com a
; pilha intacta, e o handler termina normalmente com os POPs e o
; IRETQ abaixo, como se nada de especial tivesse acontecido. Por
; isso é seguro tratar essa chamada como uma chamada de função
; comum, apesar de ela poder trocar de contexto por baixo dos panos.
extern scheduler_tick
irq0_handler:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    inc  qword [timer_ticks]

    mov  al, 0x20
    out  0x20, al           ; EOI master PIC ANTES de scheduler_tick —
                             ; se a troca de task demorar (ou a nova
                             ; task ficar rodando "para sempre" do
                             ; ponto de vista deste IRQ), o PIC não
                             ; pode ficar com o IRQ0 pendente/mascarado
                             ; à espera do EOI.

    call scheduler_tick

    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rax
    iretq

; ---- Teclado IRQ1 (INT 33) ---------------------------------------
irq1_handler:
    push rax
    push rbx
    in   al, 0x60           ; lê o scancode
    movzx rbx, byte [keyboard_hd]
    mov  [keyboard_buf + rbx], al
    inc  bl
    and  bl, 31
    cmp  bl, byte [keyboard_tl]
    je   .skip_adv
    mov  [keyboard_hd], bl
.skip_adv:
    mov  al, 0x20
    out  0x20, al           ; EOI master PIC
    pop  rbx
    pop  rax
    iretq

; ---- Mouse IRQ12 (INT 44) ----------------------------------------
irq12_handler:
    push rax
    push rbx
    in   al, 0x60
    movzx rbx, byte [mouse_hd]
    mov  [mouse_buf + rbx], al
    inc  bl
    and  bl, 31
    cmp  bl, byte [mouse_tl]
    je   .skip_adv_mouse
    mov  [mouse_hd], bl
.skip_adv_mouse:
    mov  al, 0x20
    out  0xA0, al           ; EOI slave PIC
    out  0x20, al           ; EOI master PIC
    pop  rbx
    pop  rax
    iretq

; ---- Handler padrão (IRQs ignorados) ----------------------------
default_handler:
    push rax
    mov  al, 0x20
    out  0x20, al
    out  0xA0, al
    pop  rax
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits