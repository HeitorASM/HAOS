// examples/ls/main.c — lista um diretório usando SYS_READDIR/SYS_STAT.
//
//   run ls.hexec            -> lista o diretório atual
//   run ls.hexec /bin       -> lista /bin
//   run ls.hexec notes.txt  -> mostra tipo/tamanho de um arquivo

#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : ".";
    char cwd[128];
    struct haos_stat st;
    struct haos_dirent e;

    if (getcwd(cwd, sizeof(cwd))) printf("(cwd: %s, pid %d)\n", cwd, getpid());

    if (stat(dir, &st) != 0) {
        printf("ls: '%s' nao existe\n", dir);
        return 1;
    }
    if (st.type == HAOS_TYPE_FILE) {
        printf("%s: arquivo, %u bytes\n", dir, (unsigned)st.size);
        return 0;
    }

    unsigned long i = 0;
    while (readdir(dir, i, &e) == 1) {
        if (e.type == HAOS_TYPE_DIR) printf("  d  %5u itens  %s/\n", (unsigned)e.size, e.name);
        else                         printf("  -  %5u bytes  %s\n", (unsigned)e.size, e.name);
        i++;
    }
    printf("%u entrada(s)\n", (unsigned)i);
    return 0;
}
