// examples/launcher/main.c — demonstra spawn(): um programa iniciando outro.
//
// Inicia /bin/hello.hexec duas vezes (a saida de cada filho aparece no
// mesmo terminal, prefixada com "[hello.hexec]") e depois termina. O pai
// nao espera os filhos — spawn() nao bloqueia.

#include <stdio.h>
#include <unistd.h>

int main(void) {
    puts("launcher: iniciando 2 filhos...");

    char* argv1[] = { "hello.hexec", "primeiro", NULL };
    char* argv2[] = { "hello.hexec", "segundo",  NULL };

    if (spawn("/bin/hello.hexec", argv1) != 0) puts("launcher: falha no spawn #1");
    else                                        puts("launcher: filho #1 iniciado");

    if (spawn("/bin/hello.hexec", argv2) != 0) puts("launcher: falha no spawn #2");
    else                                        puts("launcher: filho #2 iniciado");

    puts("launcher: terminei (os filhos seguem rodando)");
    return 0;
}
