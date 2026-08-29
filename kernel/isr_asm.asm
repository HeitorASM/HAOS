bits 64

extern isr_handler

section .text

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0        ; error_code = 0 (dummy)
    push qword %1        ; número do vetor
    jmp  isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1        ; número do vetor (error code já está na pilha)
    jmp  isr_common_stub
%endmacro

ISR_NOERR 0   ; #DE  Divide Error
ISR_NOERR 1   ; #DB  Debug
ISR_NOERR 2   ;      NMI
ISR_NOERR 3   ; #BP  Breakpoint
ISR_NOERR 4   ; #OF  Overflow
ISR_NOERR 5   ; #BR  Bound Range Exceeded
ISR_NOERR 6   ; #UD  Invalid Opcode
ISR_NOERR 7   ; #NM  Device Not Available
ISR_ERR   8   ; #DF  Double Fault
ISR_NOERR 9   ;      Coprocessor Segment Overrun (legado)
ISR_ERR   10  ; #TS  Invalid TSS
ISR_ERR   11  ; #NP  Segment Not Present
ISR_ERR   12  ; #SS  Stack-Segment Fault
ISR_ERR   13  ; #GP  General Protection Fault
ISR_ERR   14  ; #PF  Page Fault
ISR_NOERR 15  ;      Reservado
ISR_NOERR 16  ; #MF  x87 FPU Error
ISR_ERR   17  ; #AC  Alignment Check
ISR_NOERR 18  ; #MC  Machine Check
ISR_NOERR 19  ; #XM  SIMD FP Exception
ISR_NOERR 20  ; #VE  Virtualization Exception
ISR_ERR   21  ; #CP  Control Protection Exception
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29  ; #VC  VMM Communication Exception
ISR_ERR   30  ; #SX  Security Exception
ISR_NOERR 31

; ============================================================
;  Trecho comum — salva todo o contexto e chama o handler
;
;  Layout final da struct regs_t (kernel/isr.h) na pilha,
;  do endereço mais baixo (topo) para o mais alto:
;
;    r15, r14, r13, r12, r11, r10, r9, r8,
;    rbp, rdi, rsi, rdx, rcx, rbx, rax,
;    vector, error_code,
;    rip, cs, rflags, rsp, ss     <- empilhados pela CPU
; ============================================================
isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp         
    cld                   
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16           
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
