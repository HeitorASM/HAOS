; kernel/gdt_asm.asm — recarrega segmentos após lgdt
bits 64

global gdt_flush
gdt_flush:
    lgdt [rdi]          ; rdi = ponteiro para struct gdt_ptr
    mov  ax, 0x10       ; seletor de dados (índice 2)
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    ; recarrega CS via retorno far
    pop  rax
    push qword 0x08     ; seletor de código (índice 1)
    push rax
    retfq

; Marca stack como não-executável (suprime warning do linker)
section .note.GNU-stack noalloc noexec nowrite progbits
