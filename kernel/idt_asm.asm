; kernel/idt_asm.asm — handlers de IRQ em assembly 64-bit
bits 64

; ---- Exporta variáveis globais -----------------------------------
global timer_ticks
global kb_scancode
global kb_ready
global mouse_buf
global mouse_hd
global mouse_tl

section .bss
timer_ticks:  resq 1      ; contador de ticks do timer (100Hz)
kb_scancode:  resb 1      ; último scancode do teclado
kb_ready:     resb 1      ; 1 = novo scancode disponível
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
irq0_handler:
    push rax
    inc  qword [timer_ticks]
    mov  al, 0x20
    out  0x20, al           ; EOI master PIC
    pop  rax
    iretq

; ---- Teclado IRQ1 (INT 33) ---------------------------------------
irq1_handler:
    push rax
    in   al, 0x60
    mov  [kb_scancode], al
    mov  byte [kb_ready], 1
    mov  al, 0x20
    out  0x20, al           ; EOI master PIC
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
    je   .skip_adv
    mov  [mouse_hd], bl
.skip_adv:
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
