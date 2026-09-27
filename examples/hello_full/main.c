// examples/hello_full/main.c — testa as quatro novidades desta
// etapa de uma vez: argc/argv, malloc/free, open/read/close de
// arquivo, e agora printf/puts.
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    // ---- 1. argc/argv, agora via printf em vez de write cru ----
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("arg[%d]: %s\n", i, argv[i]);
    }

    // ---- 2. malloc/free ----
    char* buf = malloc(64);
    if (buf) {
        snprintf(buf, 64, "malloc funcionou! (endereco=%p)", (void*)buf);
        puts(buf);
        free(buf);
    } else {
        puts("malloc falhou");
    }

    // ---- 3. open/read/close ----
    int fd = open("/bin/hello.hexec");
    if (fd >= 0) {
        char header[4];
        ssize_t n = read(fd, header, 4);
        if (n == 4 && header[0] == 'H' && header[1] == 'X' &&
            header[2] == 'E' && header[3] == 'C') {
            puts("leu arquivo com sucesso (magic HXEC ok)");
        } else {
            puts("leu arquivo mas magic nao bateu");
        }
        close(fd);
    } else {
        puts("open falhou");
    }

    // ---- 4. printf com varios formatos ----
    printf("teste de formatos: %5d|%04d|%x|%X|%c|%s\n",
           42, 7, 255, 255, 'Z', "fim");

    return 0;
}
