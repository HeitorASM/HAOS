#include "storage.h"
#include "ata_pio.h"
#include "ahci.h"
#include "partition.h"
#include "../fs/haosfs.h"
#include "../kernel/memory.h"

// ---- Dispositivo "partição": traduz LBAs e limita ao intervalo ----
static BlockDevice  g_part;
static BlockDevice* g_part_parent;
static uint64_t     g_part_start;

static BlockDevice* g_selected;
static char         g_desc[96] = "nenhum";

static bool part_range_ok(uint64_t lba, uint32_t count) {
    return count && lba < g_part.sector_count && count <= g_part.sector_count - lba;
}
static BlockStatus part_read(BlockDevice* d, uint64_t lba, uint32_t count, void* buf) {
    (void)d;
    if (!part_range_ok(lba, count)) return BLOCK_ERR_INVALID;
    return g_part_parent->read(g_part_parent, g_part_start + lba, count, buf);
}
static BlockStatus part_write(BlockDevice* d, uint64_t lba, uint32_t count, const void* buf) {
    (void)d;
    if (!part_range_ok(lba, count)) return BLOCK_ERR_INVALID;
    return g_part_parent->write(g_part_parent, g_part_start + lba, count, buf);
}
static BlockStatus part_flush(BlockDevice* d) {
    (void)d;
    return g_part_parent->flush ? g_part_parent->flush(g_part_parent) : BLOCK_OK;
}

typedef enum { C_REJECT, C_FOREIGN, C_BLANK, C_RAW_HAOS, C_PART_HAOS } Cand;

static bool sector0_blank(BlockDevice* d) {
    uint8_t s[512] __attribute__((aligned(16)));
    if (d->read(d, 0, 1, s) != BLOCK_OK) return false;     // não leu: não assume "vazio"
    for (int i = 0; i < 512; i++) if (s[i]) return false;
    return true;
}

static Cand classify(BlockDevice* d, PartRange* range) {
    if (!d || !d->ready) return C_REJECT;
    bool has = false;
    PartScheme scheme = partition_scan(d, range, &has);
    if (has) return C_PART_HAOS;
    if (haosfs_probe(d) == HAOSFS_VALID) return C_RAW_HAOS;
    if (scheme != PART_NONE) return C_FOREIGN;             // tem tabela, sem partição HAOS
    if (sector0_blank(d) && haosfs_probe(d) == HAOSFS_EMPTY) return C_BLANK;
    return C_FOREIGN;                                      // tem algo que não conhecemos
}

static void set_desc(const char* name, const char* what) {
    g_desc[0] = '\0';
    kstrncat(g_desc, name, sizeof(g_desc));
    kstrncat(g_desc, ", ", sizeof(g_desc));
    kstrncat(g_desc, what, sizeof(g_desc));
}

static void select_whole(BlockDevice* d, const char* name, const char* what) {
    g_selected = d;
    set_desc(name, what);
}

static void select_partition(BlockDevice* parent, const PartRange* r,
                             const char* name) {
    g_part_parent = parent;
    g_part_start = r->start;
    g_part.sector_size = parent->sector_size;
    g_part.sector_count = r->count;
    g_part.ready = true;
    g_part.read = part_read;
    g_part.write = part_write;
    g_part.flush = part_flush;
    g_part.driver_data = NULL;
    g_selected = &g_part;
    set_desc(name, "particao HAOS");
}

void storage_init(void) {
    g_selected = NULL;
    kstrcpy(g_desc, "nenhum");

    ata_pio_init();
    int n_ahci = ahci_init();

    BlockDevice* cand[1 + AHCI_MAX_DEVICES];
    const char*  name[1 + AHCI_MAX_DEVICES];
    int nc = 0;
    if (ata_pio_device()) { cand[nc] = ata_pio_device(); name[nc] = "IDE/ATA"; nc++; }
    for (int i = 0; i < n_ahci && nc < 1 + AHCI_MAX_DEVICES; i++) {
        cand[nc] = ahci_device(i);
        name[nc] = "SATA/AHCI";
        nc++;
    }

    // Passo 1: disco que JÁ é do HAOS. Passo 2: disco em branco.
    for (int pass = 0; pass < 2 && !g_selected; pass++) {
        for (int i = 0; i < nc && !g_selected; i++) {
            PartRange r = {0, 0};
            Cand c = classify(cand[i], &r);
            if (pass == 0 && c == C_PART_HAOS)      select_partition(cand[i], &r, name[i]);
            else if (pass == 0 && c == C_RAW_HAOS)  select_whole(cand[i], name[i], "disco inteiro (HAOSFS)");
            else if (pass == 1 && c == C_BLANK)     select_whole(cand[i], name[i], "disco em branco");
        }
    }
}

BlockDevice* storage_device(void)      { return g_selected; }
const char*  storage_description(void) { return g_desc; }
