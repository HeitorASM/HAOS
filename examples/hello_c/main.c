// examples/hello_c/main.c — prova de conceito: um programa HEXEC
// escrito em C de verdade, usando a libc mínima do HAOS, em vez de
// Assembly puro chamando syscall na mão (como o antigo
// userland/hello.asm fazia).
#include <unistd.h>

int main(void) {
    const char msg[] = "Ola do HAOS, agora escrito em C de verdade!\n";
    write(msg, sizeof(msg) - 1); // -1 para não escrever o '\0' final
    return 7; // código de saída de teste, verificável via exit_code
              // da task (ver kernel/task.h) apos o processo terminar
}
