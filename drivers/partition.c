#include "partition.h"

// GUID de tipo de partição HAOS, no formato em disco (campos 1-3 little-endian).
static const uint8_t HAOS_GPT_TYPE[16] = {
    0x53, 0x4F, 0x41, 0x48, 0x53, 0x46, 0x41, 0x48,
    0x4F, 0x53, 0x46, 0x53, 0x00, 0x00, 0x00, 0x01
};

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t* p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static bool range_ok(BlockDevice* dev, uint64_t first, uint64_t count) {
    return count > 0 && first < dev->sector_count && count <= dev->sector_count - first;
}

// Tenta a GPT. true = há cabeçalho GPT válido (mesmo sem partição HAOS).
static bool scan_gpt(BlockDevice* dev, PartRange* haos, bool* has_haos) {
    uint8_t buf[512] __attribute__((aligned(16)));
    if (dev->sector_count < 3) return false;
    if (dev->read(dev, 1, 1, buf) != BLOCK_OK) return false;
    static const char sig[8] = { 'E','F','I',' ','P','A','R','T' };
    for (int i = 0; i < 8; i++) if ((char)buf[i] != sig[i]) return false;

    uint64_t entries_lba = le64(buf + 72);
    uint32_t n_entries   = le32(buf + 80);
    uint32_t esize       = le32(buf + 84);
    if (esize != 128 && esize != 256 && esize != 512) return true;   // estranha: só marca "tem GPT"
    if (n_entries == 0 || n_entries > 256) return true;
    uint32_t per_sector = 512u / esize;
    uint32_t sectors = (n_entries + per_sector - 1) / per_sector;
    if (!range_ok(dev, entries_lba, sectors)) return true;

    for (uint32_t s = 0; s < sectors; s++) {
        if (dev->read(dev, entries_lba + s, 1, buf) != BLOCK_OK) return true;
        for (uint32_t e = 0; e < per_sector; e++) {
            if (s * per_sector + e >= n_entries) break;
            const uint8_t* ent = buf + e * esize;
            bool same = true;
            for (int i = 0; i < 16; i++) if (ent[i] != HAOS_GPT_TYPE[i]) { same = false; break; }
            if (!same) continue;
            uint64_t first = le64(ent + 32), last = le64(ent + 40);
            if (last < first) continue;
            if (!range_ok(dev, first, last - first + 1)) continue;
            haos->start = first;
            haos->count = last - first + 1;
            *has_haos = true;
            return true;
        }
    }
    return true;
}

PartScheme partition_scan(BlockDevice* dev, PartRange* haos, bool* has_haos) {
    if (has_haos) *has_haos = false;
    PartRange dummy; bool dummy_b = false;
    if (!haos) haos = &dummy;
    if (!has_haos) has_haos = &dummy_b;
    if (!dev || !dev->ready || !dev->read) return PART_NONE;

    uint8_t mbr[512] __attribute__((aligned(16)));
    if (dev->read(dev, 0, 1, mbr) != BLOCK_OK) return PART_NONE;

    bool mbr_sig = (mbr[510] == 0x55 && mbr[511] == 0xAA);
    bool any_entry = false, protective = false;
    PartRange mbr_haos = {0, 0};
    bool mbr_has_haos = false;

    if (mbr_sig) {
        for (int i = 0; i < 4; i++) {
            const uint8_t* e = mbr + 446 + i * 16;
            uint8_t type = e[4];
            uint32_t first = le32(e + 8), cnt = le32(e + 12);
            if (type == 0 || cnt == 0) continue;
            any_entry = true;
            if (type == 0xEE) protective = true;
            if (type == PART_MBR_TYPE_HAOS && !mbr_has_haos && range_ok(dev, first, cnt)) {
                mbr_haos.start = first; mbr_haos.count = cnt; mbr_has_haos = true;
            }
        }
    }

    // GPT tem prioridade: com MBR protetivo (0xEE) ou mesmo sem ele, se o
    // cabeçalho existir. (Não conferimos os CRCs: só LEMOS; quem escreve
    // na partição é o HAOSFS, dentro dos limites validados aqui.)
    if (protective || !any_entry) {
        if (scan_gpt(dev, haos, has_haos)) return PART_GPT;
    } else if (scan_gpt(dev, haos, has_haos)) {
        return PART_GPT;
    }

    if (any_entry) {
        if (mbr_has_haos) { *haos = mbr_haos; *has_haos = true; }
        return PART_MBR;
    }
    return PART_NONE;
}
