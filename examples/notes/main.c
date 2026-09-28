// examples/notes/main.c — testa scanf + escrita de arquivo (SYS_WRITE_FD).
//
// Pergunta um nome de arquivo (relativo a /home/user) e uma palavra,
// grava "<palavra>\n" nele com open2(O_WRONLY|O_CREAT|O_TRUNC) +
// write_fd(), e depois reabre para leitura e imprime o que ficou
// gravado, confirmando que a escrita realmente chegou ao VFS.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char name[48];
    char word[64];
    char path[128];

    printf("Nome do arquivo: ");
    if (scanf("%s", name) != 1) { puts("entrada invalida"); return 1; }

    printf("Uma palavra para gravar: ");
    if (scanf("%s", word) != 1) { puts("entrada invalida"); return 1; }

    snprintf(path, sizeof(path), "/home/user/%s", name);

    int fd = open2(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { puts("erro: nao consegui abrir para escrita"); return 1; }

    write_fd(fd, word, strlen(word));
    write_fd(fd, "\n", 1);
    close(fd);

    printf("Gravado em %s. Lendo de volta:\n", path);

    fd = open(path);
    if (fd < 0) { puts("erro: nao consegui reabrir"); return 1; }
    char buf[80];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    printf("conteudo: %s", buf);
    return 0;
}
