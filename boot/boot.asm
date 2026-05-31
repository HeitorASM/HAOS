; ============================================================
;  HAOS
;  Multiboot2 header + transição 32-bit → 64-bit (atualizado para C++)
; ============================================================

bits 32

; ---- Multiboot2 header ----------------------------------------
section .multiboot2
align 8
mb_start:
    dd 0xE85250D6
    dd 0
    dd mb_end - mb_start
    dd -(0xE85250D6 + 0 + (mb_end - mb_start))

    ; Tag: framebuffer VBE
    align 8
    dw 5
    dw 1
    dd 20
    dd 1024
    dd 768
    dd 32

    ; Tag: fim
    align 8
    dw 0
    dw 0
    dd 8
mb_end:

; ---- BSS: stack + page tables ---------------------------------
section .bss
align 16
stack_bottom:
    resb 32768
stack_top:

align 4096
pml4:   resb 4096
pdpt:   resb 4096
pd0:    resb 4096
pd1:    resb 4096
pd2:    resb 4096
pd3:    resb 4096

; ---- Data: valores salvos do Multiboot2 -----------------------
section .data
align 8
gdt64:
.null: dq 0
.code: dq 0x00AF9A000000FFFF
.data: dq 0x00CF92000000FFFF
.ptr:
    dw $ - gdt64 - 1
    dq gdt64

global mb_magic_val
global mb_info_addr
mb_magic_val: dd 0
mb_info_addr: dd 0

; ---- Texto ----------------------------------------------------
section .text
global _start
extern kernel_main
extern cpp_init_global_ctors   ; definido em crt.cpp

_start:
    cli
    mov esp, stack_top

    mov [mb_magic_val], eax
    mov [mb_info_addr], ebx

    ; ---- Zera todas as page tables ----------------------------
    mov edi, pml4
    xor eax, eax
    mov ecx, 6 * 1024
    rep stosd

    ; ---- PML4[0] → PDPT ----
    mov eax, pdpt
    or  eax, 3
    mov [pml4], eax

    ; ---- PDPT[0..3] → PD0..PD3 (cada entrada cobre 1 GB) ----
    mov eax, pd0
    or  eax, 3
    mov [pdpt + 0*8], eax

    mov eax, pd1
    or  eax, 3
    mov [pdpt + 1*8], eax

    mov eax, pd2
    or  eax, 3
    mov [pdpt + 2*8], eax

    mov eax, pd3
    or  eax, 3
    mov [pdpt + 3*8], eax

    ; ---- PD0: 0x00000000 – 0x3FFFFFFF ----
    mov ecx, 0
    mov edi, pd0
.fill_pd0:
    mov eax, ecx
    shl eax, 21
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd0

    ; ---- PD1: 0x40000000 – 0x7FFFFFFF ----
    mov ecx, 0
    mov edi, pd1
.fill_pd1:
    mov eax, ecx
    shl eax, 21
    add eax, 0x40000000
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd1

    ; ---- PD2: 0x80000000 – 0xBFFFFFFF ----
    mov ecx, 0
    mov edi, pd2
.fill_pd2:
    mov eax, ecx
    shl eax, 21
    add eax, 0x80000000
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd2

    ; ---- PD3: 0xC0000000 – 0xFFFFFFFF ----
    mov ecx, 0
    mov edi, pd3
.fill_pd3:
    mov eax, ecx
    shl eax, 21
    add eax, 0xC0000000
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd3

    ; ---- Ativa PAE ----
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; ---- Carrega CR3 ----
    mov eax, pml4
    mov cr3, eax

    ; ---- Habilita Long Mode (EFER) ----
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; ---- Ativa paging ----
    mov eax, cr0
    or  eax, (1 << 31) | 1
    mov cr0, eax

    ; ---- Salta para código 64-bit ----
    lgdt [gdt64.ptr]
    jmp  0x08:long_mode_entry

; ---- 64-bit -------------------------------------------------------
bits 64
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp

    ; ============================================================
    ;  NOVO: Chama os construtores globais C++ (.init_array)
    ;  antes de kernel_main.
    ;  cpp_init_global_ctors está em crt.cpp e itera sobre
    ;  __init_array_start .. __init_array_end.
    ; ============================================================
    call cpp_init_global_ctors

    ; Passa argumentos Multiboot2
    mov edi, [mb_magic_val]
    mov esi, [mb_info_addr]

    call kernel_main

.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
