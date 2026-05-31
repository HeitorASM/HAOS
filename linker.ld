/* linker.ld — HAOS x86-64 (atualizado para C++17) */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    . = 1M;

    /* Cabeçalho Multiboot2 (primeiro na imagem) */
    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    /* Código executável */
    .text ALIGN(4K) : {
        *(.text*)
    }

    /* Dados somente-leitura (vtables, strings literais, etc.) */
    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    /* Dados inicializados */
    .data ALIGN(4K) : {
        *(.data*)
    }

    /*
     * .init_array — construtores de objetos globais/estáticos C++
     * Os símbolos __init_array_start e __init_array_end são usados
     * por crt.cpp para chamar os construtores antes de kernel_main.
     */
    .init_array ALIGN(8) : {
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);
    }

    /* BSS (variáveis não inicializadas) */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
        _kernel_end = .;   /* Marca o fim do kernel — usado pelo PFA */
    }
}
