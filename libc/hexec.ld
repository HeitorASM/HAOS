/* libc/hexec.ld — linker script para programas HEXEC v2
 *
 * Produz um arquivo com CADA seção alinhada a página (4KB), o que
 * permite ao loader do kernel (process.c) mapear .text, .rodata e
 * .data/.bss com permissões DIFERENTES (RX, RO, RW+NX respectivamente)
 * — W^X de verdade, em vez do bloco único RW+USER da v1. Ver o
 * comentário extenso em kernel/hexec.h sobre o formato v2.
 *
 * Os símbolos __hexec_*_start/__hexec_*_end abaixo marcam onde cada
 * seção começa/termina — libc/mkhexec.py lê esses símbolos (via
 * `nm`) para calcular text_size/rodata_size/data_size/bss_size
 * exatos, sem precisar adivinhar ou recalcular por conta própria.
 *
 * HEXEC_LOAD_ADDR (0x300000000) TEM que bater com kernel/hexec.h —
 * se um dia o kernel mudar esse valor, este arquivo precisa ser
 * atualizado junto, senão o programa calcularia endereços relativos
 * ao endereço errado.
 *
 * _start (crt0.asm) é forçado a ser o primeiro símbolo do .text (ver
 * ENTRY(_start) e a ordem dos .o no Makefile: crt0.o sempre primeiro)
 * — todo programa gerado por este script usa entry_offset=0.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x300000000; /* HEXEC_LOAD_ADDR — ver kernel/hexec.h */

    .text : {
        __hexec_text_start = .;
        *(.text._start) /* crt0.asm garante que _start cai aqui —
                            ver comentário acima sobre a ordem dos .o */
        *(.text*)
        . = ALIGN(4096); /* próxima seção começa em página nova —
                             necessário para dar a ela permissões
                             DIFERENTES sem misturar com .text numa
                             mesma página física. */
        __hexec_text_end = .;
    }

    .rodata : {
        __hexec_rodata_start = .;
        *(.rodata*)
        . = ALIGN(4096);
        __hexec_rodata_end = .;
    }

    .data : {
        __hexec_data_start = .;
        *(.data*)
        . = ALIGN(4096);
        __hexec_data_end = .;
    }

    /* .bss fica marcado como NOLOAD: o linker calcula seu tamanho
     * normalmente (variáveis globais zeradas), mas NÃO escreve seus
     * bytes no arquivo de saída — o loader do kernel é quem aloca e
     * zera essas páginas em tempo de carga, sem precisar que o
     * arquivo em disco contenha um monte de zeros desperdiçados. */
    .bss (NOLOAD) : {
        __hexec_bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4096);
        __hexec_bss_end = .;
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
