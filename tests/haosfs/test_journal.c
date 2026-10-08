// tests/haosfs/test_journal.c — "queda de energia" no HAOSFS.
//
// Usa o vfs.c e o haosfs.c REAIS sobre um disco simulado em memória que
// para de gravar depois de N setores (a gravação de um bloco pode ficar
// pela metade). Para cada ponto de queda, "reinicia" (vfs_init) e exige que
// o sistema de arquivos monte e esteja EXATAMENTE no estado antigo OU no
// novo — nunca num meio-termo.
//
// Modelo simplificado: as gravações são duráveis na ordem em que chegam
// (não simula o disco reordenando o cache entre dois FLUSH).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/types.h"
#include "../../fs/vfs.h"
#include "../../drivers/block.h"
#include "../../drivers/storage.h"

#define SECTORS 32768u                 // 16 MiB: cabe o journal de 4 MiB
static uint8_t* g_mem;
static uint8_t* g_base;
static long     g_budget = -1;         // setores que ainda "chegam" ao disco; -1 = sem limite
static long     g_total_written;
static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)

static BlockStatus dev_read(BlockDevice* d, uint64_t lba, uint32_t n, void* buf) {
    (void)d;
    if (lba + n > SECTORS) return BLOCK_ERR_INVALID;
    memcpy(buf, g_mem + lba * 512, (size_t)n * 512);
    return BLOCK_OK;
}
static BlockStatus dev_write(BlockDevice* d, uint64_t lba, uint32_t n, const void* buf) {
    (void)d;
    if (lba + n > SECTORS) return BLOCK_ERR_INVALID;
    for (uint32_t i = 0; i < n; i++) {
        if (g_budget == 0) continue;              // energia caiu: o setor se perde
        memcpy(g_mem + (lba + i) * 512, (const uint8_t*)buf + (size_t)i * 512, 512);
        g_total_written++;
        if (g_budget > 0) g_budget--;
    }
    return BLOCK_OK;                              // o SO não percebe a queda
}
static BlockStatus dev_flush(BlockDevice* d) { (void)d; return BLOCK_OK; }
static BlockDevice g_dev = { 512, SECTORS, 1, dev_read, dev_write, NULL, dev_flush };
BlockDevice* storage_device(void) { return &g_dev; }

static char g_big[20001];
static const char* big(char c) { memset(g_big, c, 20000); g_big[20000] = 0; return g_big; }

static void apply_update(void) {                  // estado antigo -> novo
    vfs_batch_begin();
    VfsNode* a = vfs_find_child(vfs_root(), "a.txt");
    if (a) vfs_write(a, big('B'));
    VfsNode* b = vfs_touch(vfs_root(), "b.txt");
    if (b) vfs_write(b, "BBBB");
    vfs_batch_end();
}
static bool is_old(void) {
    VfsNode* a = vfs_find_child(vfs_root(), "a.txt");
    return a && a->data && strcmp(a->data, "AAAA") == 0 && !vfs_find_child(vfs_root(), "b.txt");
}
static bool is_new(void) {
    VfsNode* a = vfs_find_child(vfs_root(), "a.txt");
    VfsNode* b = vfs_find_child(vfs_root(), "b.txt");
    return a && a->size == 20000 && a->data && a->data[0] == 'B' && a->data[19999] == 'B' &&
           b && b->data && strcmp(b->data, "BBBB") == 0;
}

int main(void) {
    g_mem  = calloc(SECTORS, 512);
    g_base = calloc(SECTORS, 512);

    printf("[1] formata disco em branco e cria o estado antigo\n");
    vfs_init();
    CHECK(vfs_find_child(vfs_root(), "bin") != NULL);
    VfsNode* a = vfs_touch(vfs_root(), "a.txt");
    CHECK(a && vfs_write(a, "AAAA") && vfs_sync());
    vfs_init();                                    // remonta: tem que persistir
    CHECK(is_old());
    memcpy(g_base, g_mem, (size_t)SECTORS * 512);

    printf("[2] sem queda: o sync completo leva ao estado novo\n");
    g_total_written = 0;
    apply_update();
    long full_cost = g_total_written;
    vfs_init();
    CHECK(is_new());
    printf("    (um sync desses grava %ld setores)\n", full_cost);

    printf("[3] sync sem mudanca nao grava nada\n");
    g_total_written = 0;
    CHECK(vfs_sync());
    CHECK(g_total_written == 0);

    printf("[4] queda de energia em cada ponto do sync\n");
    int n_old = 0, n_new = 0, n_bad = 0;
    for (long k = 0; k <= full_cost + 16; k += 3) {
        memcpy(g_mem, g_base, (size_t)SECTORS * 512);
        g_budget = -1;
        vfs_init();
        if (!is_old()) { n_bad++; continue; }
        g_budget = k;
        apply_update();                            // pode "perder" gravações a partir de k
        g_budget = -1;
        vfs_init();                                // reinicia: recuperação roda aqui
        if (is_old()) n_old++;
        else if (is_new()) n_new++;
        else { n_bad++; printf("    estado inconsistente com queda em k=%ld\n", k); }
    }
    printf("    antigo=%d novo=%d inconsistente=%d\n", n_old, n_new, n_bad);
    CHECK(n_bad == 0);
    CHECK(n_old > 0 && n_new > 0);

    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
