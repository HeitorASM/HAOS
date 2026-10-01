// examples/launcher/main.c — demonstra spawn() + waitpid(): um programa
// iniciando outro e esperando cada um terminar antes de seguir.
//
// Inicia /bin/hello.hexec duas vezes (a saída de cada filho aparece no
// mesmo terminal, prefixada com "[hello.hexec]"), espera CADA UM
// terminar (nessa ordem: primeiro espera o #1, só depois o #2 — não é
// "espera o que terminar primeiro"), e imprime o código de saída de
// cada um antes de encerrar.

#include <stdio.h>
#include <unistd.h>

int main(void) {
    puts("launcher: iniciando 2 filhos...");

    char* argv1[] = { "hello.hexec", "primeiro", NULL };
    char* argv2[] = { "hello.hexec", "segundo",  NULL };

    int pid1 = spawn("/bin/hello.hexec", argv1);
    if (pid1 < 0) { puts("launcher: falha no spawn #1"); return 1; }
    printf("launcher: filho #1 iniciado (pid=%d)\n", pid1);

    int pid2 = spawn("/bin/hello.hexec", argv2);
    if (pid2 < 0) { puts("launcher: falha no spawn #2"); return 1; }
    printf("launcher: filho #2 iniciado (pid=%d)\n", pid2);

    puts("launcher: esperando filho #1 terminar...");
    unsigned long code1 = 0;
    int r1 = waitpid(pid1, &code1);
    // printf() desta libc não suporta %lu (sem modificador de
    // tamanho 'l' nem tipos de 64 bits em varargs — ver comentário em
    // libc/src/stdio/printf.c), então truncamos para 32 bits antes de
    // passar a %u. Suficiente para um código de saída normal; um
    // processo morto por Ctrl+C tem exit_code = (uint64_t)-1 (ver
    // task_kill), que truncado vira 4294967295 — ainda reconhecível
    // como "não foi um retorno normal".
    printf("launcher: waitpid(#1) = %d, codigo = %u\n", r1, (unsigned int)code1);

    puts("launcher: esperando filho #2 terminar...");
    unsigned long code2 = 0;
    int r2 = waitpid(pid2, &code2);
    printf("launcher: waitpid(#2) = %d, codigo = %u\n", r2, (unsigned int)code2);

    puts("launcher: os dois filhos terminaram, terminei tambem");
    return 0;
}
