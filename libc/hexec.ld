/* libc/hexec.ld — linker script para programas HEXEC
 *
 * Layout de saída: um único blob contíguo (.text + .rodata + .data),
 * carregado a partir de HEXEC_LOAD_ADDR (ver kernel/hexec.h — este
 * valor TEM que bater com aquele; se um dia o kernel mudar
 * HEXEC_LOAD_ADDR, este arquivo precisa ser atualizado junto, senão
 * o programa vai calcular endereços relativos ao endereço errado).
 *
 * .bss NÃO entra no arquivo de saída (ver o AT> abaixo e o script
 * mkhexec.py, que lê o tamanho de .bss separadamente do binário e
 * escreve isso no header HEXEC como bss_size) — o loader do kernel
 * é quem aloca e zera essas páginas em tempo de carga, sem precisar
 * que o arquivo em disco contenha um monte de zeros desperdiçados.
 *
 * _start (crt0.asm) é forçado a ser o primeiro símbolo do .text,
 * então ENTRY(_start) e a ordem dos arquivos no Makefile (crt0.o
 * sempre primeiro) garantem que ele fica exatamente no offset 0 —
 * e por isso todo programa gerado por este script usa
 * entry_offset=0 no header HEXEC (ver mkhexec.py).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x300000000; /* HEXEC_LOAD_ADDR — ver kernel/hexec.h */

    .text : {
        *(.text._start) /* crt0.asm garante que _start cai aqui,
                            mas o mais importante é a ORDEM dos .o
                            passados ao linker (crt0.o primeiro) —
                            ver libc/Makefile e o Makefile de cada
                            programa de exemplo. */
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    /* .bss fica marcado como NOLOAD: o linker calcula seu tamanho
     * normalmente (variáveis globais zeradas), mas NÃO escreve seus
     * bytes no arquivo de saída — extraímos esse tamanho depois com
     * `objcopy`/`size` no Makefile, para preencher bss_size no
     * header HEXEC (ver mkhexec.py). */
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }

    /* Descarta seções que não fazem sentido num binário flat sem
     * carregador de SO hospedeiro por trás (sem dynamic linking,
     * sem informação de debug no artefato final). */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
    }
}
