bits 64

global gdt_flush
gdt_flush:
    lgdt [rdi]          ; rdi = ponteiro para struct gdt_ptr
    mov  ax, 0x10       ; seletor de dados de kernel (índice 2)
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    ; recarrega CS via retorno far
    pop  rax
    push qword 0x08     ; seletor de código de kernel (índice 1)
    push rax
    retfq

; ---- Carrega o Task Register com o seletor da TSS ----------------
; LTR só pode ser executado depois de LGDT (a TSS precisa já estar
; na tabela). O seletor 0x28 aponta para o descriptor de 16 bytes
; montado em gdt_set_tss() dentro de gdt.c.
global tss_flush
tss_flush:
    mov  ax, 0x28
    ltr  ax
    ret

; Marca stack como não-executável (suprime warning do linker)
section .note.GNU-stack noalloc noexec nowrite progbits
