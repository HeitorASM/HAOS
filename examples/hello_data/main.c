// examples/hello_data/main.c — valida .data (inicializada) e .bss
// (não inicializada) sendo mapeadas com permissões corretas (RW+NX)
// pelo loader HEXEC v2, separadas de .text (RX).
#include <unistd.h>
#include <string.h>

static char initialized_msg[] = "Contador: 0\n"; // vai para .data
                                                    // (tem valor inicial
                                                    // != 0, então o
                                                    // linker não pode
                                                    // tratar como .bss)
static int  counter_bss;                            // vai para .bss
                                                    // (inicializado a
                                                    // zero implicitamente
                                                    // -- o padrão do C)

int main(void) {
    counter_bss = 42; // prova que .bss é gravável (RW) mesmo não
                        // tendo bytes no arquivo em disco
    initialized_msg[10] = '0' + (counter_bss % 10); // prova que .data
                                                        // é gravável
    write(initialized_msg, strlen(initialized_msg));
    return counter_bss;
}
