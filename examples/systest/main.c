// examples/systest/main.c — autoteste das syscalls 9..19 e das
// correções de segurança do kernel. Rode com "run systest.hexec" e
// confira se TODAS as linhas mostram [PASS].
//
// Cobre: getpid, yield, uptime_ms, sleep_ms, stat, readdir, getcwd,
// chdir, mkdir, unlink, lseek, caminhos relativos (cwd por processo),
// fd de arquivo removido, nome de arquivo longo (overflow no VFS) e
// ponteiros inválidos (overflow em validate_user_range / buffer RO).

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "haos/syscall.h"
#include "haos/gfx.h"

static int g_fail = 0;

static void check(const char* name, int ok) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

int main(void) {
    struct haos_stat st;
    struct haos_dirent e;
    char buf[32];
    char cwd[128];

    puts("== systest: syscalls 9..19 ==");

    // ---- Limpeza de restos de execuções anteriores (ignora erros) ----
    unlink("/tmp/systest/a.txt");
    unlink("/tmp/systest/sub");
    unlink("/tmp/systest");

    // ---- Identidade e tempo ----
    check("getpid() > 0", getpid() > 0);
    yield();
    check("yield() retorna", 1);

    unsigned long t0 = uptime_ms();
    sleep_ms(200);
    unsigned long dt = uptime_ms() - t0;
    printf("      (sleep_ms(200) levou %u ms)\n", (unsigned)dt);
    check("sleep_ms(200): 200 <= dt < 2000", dt >= 200 && dt < 2000);

    // ---- mkdir / stat / cwd ----
    check("getcwd() devolve caminho absoluto",
          getcwd(cwd, sizeof(cwd)) != NULL && cwd[0] == '/');
    check("mkdir(/tmp/systest) == 0", mkdir("/tmp/systest") == 0);
    check("mkdir repetido falha (ja existe)", mkdir("/tmp/systest") == -1);
    check("mkdir com pai inexistente falha", mkdir("/tmp/nao_existe/x") == -1);
    check("stat(/tmp/systest) e diretorio vazio",
          stat("/tmp/systest", &st) == 0 && st.type == HAOS_TYPE_DIR && st.size == 0);
    check("stat de caminho inexistente == -1", stat("/tmp/nada", &st) == -1);

    check("chdir(/tmp/systest) == 0", chdir("/tmp/systest") == 0);
    check("getcwd() == /tmp/systest",
          getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, "/tmp/systest") == 0);
    check("chdir para inexistente falha", chdir("/tmp/systest/nada") == -1);
    check("getcwd com buffer pequeno == NULL", getcwd(buf, 4) == NULL);

    // ---- Arquivo com caminho RELATIVO + lseek ----
    int fd = open2("a.txt", O_WRONLY | O_CREAT | O_TRUNC);
    check("open2(a.txt, CREAT) relativo ao cwd", fd >= 3);
    check("write_fd 10 bytes", write_fd(fd, "HelloWorld", 10) == 10);
    check("lseek(CUR,0) == 10", lseek(fd, 0, SEEK_CUR) == 10);
    check("lseek(SET,5) == 5", lseek(fd, 5, SEEK_SET) == 5);
    check("write_fd sobrescreve no meio", write_fd(fd, "HAOS!", 5) == 5);
    check("lseek(END,0) == 10", lseek(fd, 0, SEEK_END) == 10);
    check("lseek negativo antes do inicio == -1", lseek(fd, -100, SEEK_SET) == -1);
    close(fd);

    check("stat(a.txt) arquivo de 10 bytes",
          stat("a.txt", &st) == 0 && st.type == HAOS_TYPE_FILE && st.size == 10);

    fd = open("a.txt");
    memset(buf, 0, sizeof(buf));
    check("read 10 bytes", read(fd, buf, 10) == 10);
    check("conteudo == HelloHAOS!", strncmp(buf, "HelloHAOS!", 10) == 0);
    check("lseek(END,-4) == 6", lseek(fd, -4, SEEK_END) == 6);
    memset(buf, 0, sizeof(buf));
    check("read 4 bytes depois do seek", read(fd, buf, 4) == 4 && strncmp(buf, "AOS!", 4) == 0);
    check("lseek em fd invalido == -1", lseek(31, 0, SEEK_SET) == -1);
    check("lseek em stdin (fd 0) == -1", lseek(0, 0, SEEK_SET) == -1);
    close(fd);

    // ---- readdir ----
    check("mkdir(sub) relativo", mkdir("sub") == 0);
    int count = 0, saw_a = 0, saw_sub = 0;
    unsigned long i;
    for (i = 0; i < 16 && readdir(".", i, &e) == 1; i++) {
        count++;
        if (strcmp(e.name, "a.txt") == 0 && e.type == HAOS_TYPE_FILE) saw_a = 1;
        if (strcmp(e.name, "sub") == 0 && e.type == HAOS_TYPE_DIR) saw_sub = 1;
    }
    check("readdir(.) lista exatamente a.txt e sub", count == 2 && saw_a && saw_sub);
    check("readdir alem do fim == 0", readdir(".", 2, &e) == 0);
    check("readdir em arquivo == -1", readdir("a.txt", 0, &e) == -1);

    // ---- unlink e fd de arquivo removido ----
    check("unlink(sub) vazio == 0", unlink("sub") == 0);
    check("unlink de diretorio nao vazio falha", unlink("/tmp/systest") == -1);
    int fdr = open("a.txt");                  // aberto para leitura
    int fdw = open2("a.txt", O_WRONLY);       // aberto para escrita
    check("abriu fds de leitura e escrita", fdr >= 3 && fdw >= 3);
    check("unlink(a.txt) com fds abertos == 0", unlink("a.txt") == 0);
    check("read em fd de arquivo removido == -1", read(fdr, buf, 1) == -1);
    check("write_fd em fd removido == -1", write_fd(fdw, "x", 1) == -1);
    check("lseek em fd removido == -1", lseek(fdr, 0, SEEK_SET) == -1);
    close(fdr);
    close(fdw);
    check("stat(a.txt) removido == -1", stat("a.txt", &st) == -1);

    check("chdir(/) == 0", chdir("/") == 0);
    check("unlink(/tmp/systest) == 0", unlink("/tmp/systest") == 0);
    check("unlink(/) recusado", unlink("/") == -1);

    // ---- Correcoes de seguranca ----
    char longname[130];
    memcpy(longname, "/tmp/", 5);
    memset(longname + 5, 'A', 120);
    longname[125] = '\0';
    check("nome de arquivo longo (120) e recusado (overflow no VFS)",
          open2(longname, O_WRONLY | O_CREAT) == -1);

    // Ponteiro de kernel que "da a volta": addr + len estoura uint64.
    long r = haos_syscall(SYS_WRITE, (long)0xFFFFFFFFFFFFFF80UL, 200, 0, 0, 0);
    check("sys_write com ponteiro de kernel (wraparound) == -1", r == -1);

    // Destino somente-leitura (.text): validacao ANTES de bloquear em stdin.
    check("read(stdin) em buffer RO (.text) == -1",
          read(0, (void*)main, 1) == -1);

    // ---- Janelas graficas (SYS_WIN_*) ----
    // Cria uma janelinha de verdade (some sozinha ao final). Precisa de
    // vaga no WM (MAX_WINDOWS): se falhar aqui, feche alguma janela.
    puts("== janelas (SYS_WIN_*) ==");
    int w = win_create(200, 120, "systest");
    check("win_create devolve handle >= 0", w >= 0);

    unsigned ww = 0, wh = 0;
    check("win_size == 200x120", win_size(w, &ww, &wh) == 0 && ww == 200 && wh == 120);
    check("win_fill_rect", win_fill_rect(w, 0, 0, 200, 120, 0x203060) == 0);
    check("win_fill_rect gigante e fora do canvas e recortado",
          win_fill_rect(w, -50, -50, 100000, 100000, 0x203060) == 0);
    check("win_text_bg", win_text_bg(w, 8, 8, "systest: janela ok", 0xFFFFFF, 0x203060) == 0);

    uint32_t px[16];
    for (int k = 0; k < 16; k++) px[k] = 0xFF8800;
    check("win_blit 4x4", win_blit(w, 10, 40, 4, 4, px) == 0);
    check("win_blit parcialmente fora do canvas", win_blit(w, -2, -2, 4, 4, px) == 0);

    struct haos_win_event wev;
    check("win_event sem eventos (consulta) == 0", win_event(w, &wev, 0) == 0);
    unsigned long tw0 = uptime_ms();
    check("win_event com timeout 100 ms == 0 e espera >= 100 ms",
          win_event(w, &wev, 100) == 0 && uptime_ms() - tw0 >= 100);

    check("handle inexistente == -1", win_fill_rect(15, 0, 0, 1, 1, 0) == -1);
    check("handle negativo == -1", win_fill_rect(-1, 0, 0, 1, 1, 0) == -1);
    check("win_blit com ponteiro de kernel (wraparound) == -1",
          win_blit(w, 0, 0, 4, 4, (const uint32_t*)0xFFFFFFFFFFFFFF80UL) == -1);
    check("win_event com destino somente-leitura (.text) == -1",
          win_event(w, (struct haos_win_event*)(void*)main, 0) == -1);

    check("win_destroy == 0", win_destroy(w) == 0);
    check("handle destruido == -1", win_fill_rect(w, 0, 0, 1, 1, 0) == -1);
    check("win_destroy repetido == -1", win_destroy(w) == -1);

    printf("== %s (%d falha(s)) ==\n", g_fail == 0 ? "TUDO OK" : "HOUVE FALHAS", g_fail);
    return g_fail;
}
