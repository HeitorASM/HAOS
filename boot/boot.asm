; ============================================================
;  HAOS  —  boot/boot.asm  (corrigido: identity map 4GB)
;  Multiboot2 header + transição 32-bit → 64-bit (Long Mode)
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
pml4:   resb 4096       ; nível 4 (1 entrada usada por GB)
pdpt:   resb 4096       ; nível 3 (4 entradas = 4 GB)
pd0:    resb 4096       ; nível 2 para 0GB–1GB
pd1:    resb 4096       ; nível 2 para 1GB–2GB
pd2:    resb 4096       ; nível 2 para 2GB–3GB
pd3:    resb 4096       ; nível 2 para 3GB–4GB   ← cobre 0xE0000000

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

_start:
    cli
    mov esp, stack_top

    mov [mb_magic_val], eax
    mov [mb_info_addr], ebx

    ; ---- Zera todas as page tables (PML4 + PDPT + PD0..PD3) ----
    mov edi, pml4
    xor eax, eax
    mov ecx, 6 * 1024      ; 6 páginas × 1024 dwords
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

    ; ---- Preenche PD0: mapeia 0x00000000 – 0x3FFFFFFF ----
    mov ecx, 0
    mov edi, pd0
.fill_pd0:
    mov eax, ecx
    shl eax, 21
    or  eax, 0x83           ; present + writable + huge (2MB)
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd0

    ; ---- Preenche PD1: mapeia 0x40000000 – 0x7FFFFFFF ----
    mov ecx, 0
    mov edi, pd1
.fill_pd1:
    ; endereço físico = 1GB + ecx * 2MB
    mov eax, ecx
    shl eax, 21
    add eax, 0x40000000     ; base 1GB
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd1

    ; ---- Preenche PD2: mapeia 0x80000000 – 0xBFFFFFFF ----
    mov ecx, 0
    mov edi, pd2
.fill_pd2:
    mov eax, ecx
    shl eax, 21
    add eax, 0x80000000     ; base 2GB
    or  eax, 0x83
    mov dword [edi + ecx*8],     eax
    mov dword [edi + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl  .fill_pd2

    ; ---- Preenche PD3: mapeia 0xC0000000 – 0xFFFFFFFF ----
    ; (cobre 0xE0000000 = framebuffer do VirtualBox/QEMU)
    mov ecx, 0
    mov edi, pd3
.fill_pd3:
    mov eax, ecx
    shl eax, 21
    ; ATENÇÃO: para endereços >= 0xC0000000, o valor fica >= 3GB
    ; EAX pode transbordar 32 bits a partir de ecx=96 (96*2MB = 192MB + 3GB = 0xCC000000, ok)
    ; Precisamos guardar a parte alta em EDX
    ; Usa arithmetic diferente: base 3GB = 0xC0000000
    add eax, 0xC0000000
    ; A parte alta (bits 32+) sempre é 0 para endereços < 4GB
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

    ; ---- Habilita Long Mode no EFER ----
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

    ; Reconfigura a stack em 64-bit
    ; (usa endereço absoluto para garantir que funciona)
    mov rsp, stack_top
    xor rbp, rbp

    ; Passa argumentos: magic (RDI), info_addr (RSI)
    mov edi, [mb_magic_val]
    mov esi, [mb_info_addr]

    call kernel_main

.halt:
    hlt
    jmp .halt

; Marca stack como não-executável
section .note.GNU-stack noalloc noexec nowrite progbits
