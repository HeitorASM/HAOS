// Teste de host do leitor de partições (drivers/partition.c) com um "disco"
// em memória. Compila só partition.c + este arquivo — NÃO o kernel.
#include <stdio.h>
#include <string.h>
#include "../../kernel/types.h"
#include "../../drivers/block.h"
#include "../../drivers/partition.h"

#define DISK_SECTORS 100000u
static uint8_t g_img[8][512] __attribute__((aligned(16)));   // só os 8 primeiros setores importam
static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)

static BlockStatus rd(BlockDevice* d, uint64_t lba, uint32_t n, void* buf) {
    (void)d;
    if (lba + n > 8) { memset(buf, 0, (size_t)n * 512); return BLOCK_OK; }
    memcpy(buf, g_img[lba], (size_t)n * 512);
    return BLOCK_OK;
}
static BlockDevice g_dev = { 512, DISK_SECTORS, 1, rd, NULL, NULL, NULL };

static void put32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put64(uint8_t* p, uint64_t v) { put32(p, (uint32_t)v); put32(p+4, (uint32_t)(v>>32)); }
static void mbr_entry(int i, uint8_t type, uint32_t first, uint32_t cnt) {
    uint8_t* e = g_img[0] + 446 + i * 16;
    e[4] = type; put32(e + 8, first); put32(e + 12, cnt);
    g_img[0][510] = 0x55; g_img[0][511] = 0xAA;
}
static const uint8_t HAOS_GUID[16] = { 0x53,0x4F,0x41,0x48,0x53,0x46,0x41,0x48,0x4F,0x53,0x46,0x53,0,0,0,1 };

int main(void) {
    PartRange r; bool has;

    printf("[1] disco em branco\n");
    memset(g_img, 0, sizeof g_img);
    CHECK(partition_scan(&g_dev, &r, &has) == PART_NONE && !has);

    printf("[2] MBR com particao 0xDA\n");
    memset(g_img, 0, sizeof g_img);
    mbr_entry(0, 0x83, 2048, 5000);
    mbr_entry(1, 0xDA, 10000, 20000);
    CHECK(partition_scan(&g_dev, &r, &has) == PART_MBR && has && r.start == 10000 && r.count == 20000);

    printf("[3] MBR 'estrangeiro' (sem 0xDA): tem tabela, sem particao HAOS\n");
    memset(g_img, 0, sizeof g_img);
    mbr_entry(0, 0x07, 2048, 50000);
    CHECK(partition_scan(&g_dev, &r, &has) == PART_MBR && !has);

    printf("[4] 0xDA fora do disco e' ignorada\n");
    memset(g_img, 0, sizeof g_img);
    mbr_entry(0, 0xDA, 90000, 50000);
    CHECK(partition_scan(&g_dev, &r, &has) == PART_MBR && !has);

    printf("[5] GPT com particao HAOS (MBR protetivo)\n");
    memset(g_img, 0, sizeof g_img);
    mbr_entry(0, 0xEE, 1, DISK_SECTORS - 1);
    memcpy(g_img[1], "EFI PART", 8);
    put64(g_img[1] + 72, 2); put32(g_img[1] + 80, 4); put32(g_img[1] + 84, 128);
    g_img[2][0] = 0xAA; put64(g_img[2] + 32, 100); put64(g_img[2] + 40, 200);      // outra particao
    memcpy(g_img[2] + 128, HAOS_GUID, 16);
    put64(g_img[2] + 128 + 32, 4096); put64(g_img[2] + 128 + 40, 8191);
    CHECK(partition_scan(&g_dev, &r, &has) == PART_GPT && has && r.start == 4096 && r.count == 4096);

    printf("[6] GPT sem particao HAOS\n");
    g_img[2][128] = 0x00;                    // quebra o GUID da 2a entrada
    CHECK(partition_scan(&g_dev, &r, &has) == PART_GPT && !has);

    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
