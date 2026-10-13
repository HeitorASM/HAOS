// drivers/ahci.c — AHCI 1.x por polling, um comando por vez (slot 0).
//
// Fluxo: PCI (classe 01:06:01) -> BAR5 (ABAR) mapeado SEM cache -> para cada
// porta com disco SATA: para o motor, aponta Command List/FIS, religa,
// IDENTIFY -> BlockDevice com read/write/flush.
//
// DMA: o PRDT é montado página a página usando paging_translate(), então o
// buffer do chamador pode estar em qualquer lugar (heap, pilha de task...),
// sem exigir memória fisicamente contígua.
#include "ahci.h"
#include "pci.h"
#include "../kernel/memory.h"
#include "../kernel/paging.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

// ---- Registradores do HBA ----
#define HBA_CAP2 0x24
#define HBA_GHC  0x04
#define HBA_PI   0x0C
#define HBA_BOHC 0x28
#define GHC_AE   (1u << 31)

// ---- Registradores de porta (offset 0x100 + porta*0x80) ----
#define P_CLB  0x00
#define P_CLBU 0x04
#define P_FB   0x08
#define P_FBU  0x0C
#define P_IS   0x10
#define P_IE   0x14
#define P_CMD  0x18
#define P_TFD  0x20
#define P_SIG  0x24
#define P_SSTS 0x28
#define P_SERR 0x30
#define P_SACT 0x34
#define P_CI   0x38

#define CMD_ST  (1u << 0)
#define CMD_FRE (1u << 4)
#define CMD_FR  (1u << 14)
#define CMD_CR  (1u << 15)
#define TFD_ERR 0x01u
#define TFD_DRQ 0x08u
#define TFD_DF  0x20u
#define TFD_BSY 0x80u
#define IS_TFES (1u << 30)
#define SIG_ATA 0x00000101u

#define ATA_READ_DMA_EXT  0x25
#define ATA_WRITE_DMA_EXT 0x35
#define ATA_FLUSH_EXT     0xEA
#define ATA_IDENTIFY      0xEC

#define MAX_PRD          32
#define MAX_SECTORS_CMD  128u          // 64 KiB por comando (<= 17 PRDs)
#define TIMEOUT_IO       3000000000ULL // ciclos de TSC (~1 s)
#define TIMEOUT_STOP     1500000000ULL

typedef struct {
    volatile uint8_t* regs;   // base dos registradores da porta
    uint8_t* clb;             // Command List (1 KiB) — endereço físico == virtual
    uint8_t* ctba;            // Command Table do slot 0
    uint8_t* bounce;          // página para buffers com endereço ímpar
    BlockDevice dev;
} AhciPort;

static volatile uint8_t* g_hba;
static AhciPort g_ports[AHCI_MAX_DEVICES];
static int g_count;

static inline uint32_t rd32(volatile uint8_t* b, uint32_t off) {
    return *(volatile uint32_t*)(b + off);
}
static inline void wr32(volatile uint8_t* b, uint32_t off, uint32_t v) {
    *(volatile uint32_t*)(b + off) = v;
}
static inline void full_barrier(void) { __asm__ volatile ("mfence" ::: "memory"); }

// Espera (rd32(off) & mask) == 0. 0 = ok, -1 = estourou o tempo.
static int wait_clear(volatile uint8_t* b, uint32_t off, uint32_t mask, uint64_t cycles) {
    uint64_t deadline = rdtsc() + cycles;
    while (rd32(b, off) & mask) {
        if (rdtsc() > deadline) return -1;
        __asm__ volatile ("pause");
    }
    return 0;
}

static void zero_page(void* p) {
    uint64_t* q = (uint64_t*)p;
    for (int i = 0; i < 512; i++) q[i] = 0;
}

static int port_stop(volatile uint8_t* r) {
    wr32(r, P_CMD, rd32(r, P_CMD) & ~CMD_ST);
    if (wait_clear(r, P_CMD, CMD_CR, TIMEOUT_STOP)) return -1;
    wr32(r, P_CMD, rd32(r, P_CMD) & ~CMD_FRE);
    if (rd32(r, P_CMD) & CMD_FR)
        if (wait_clear(r, P_CMD, CMD_FR, TIMEOUT_STOP)) return -1;
    return 0;
}

static int port_start(volatile uint8_t* r) {
    if (wait_clear(r, P_CMD, CMD_CR, TIMEOUT_STOP)) return -1;
    wr32(r, P_CMD, rd32(r, P_CMD) | CMD_FRE);
    wr32(r, P_CMD, rd32(r, P_CMD) | CMD_ST);
    return 0;
}

// Depois de um erro: para/religa a porta para limpar o estado do HBA.
static void port_recover(AhciPort* p) {
    volatile uint8_t* r = p->regs;
    port_stop(r);
    wr32(r, P_SERR, 0xFFFFFFFFu);
    wr32(r, P_IS, 0xFFFFFFFFu);
    port_start(r);
}

// Monta o PRDT para `bytes` a partir de `buf`. Devolve nº de entradas ou -1.
static int build_prdt(uint8_t* ctba, uint64_t v, uint32_t bytes) {
    uint8_t* prdt = ctba + 0x80;
    int n = 0;
    while (bytes) {
        uint64_t phys;
        if (!paging_translate(v & ~0xFFFULL, &phys)) return -1;
        phys += v & 0xFFFULL;
        if (phys >= 0x100000000ULL) return -1;       // só usamos 32 bits de endereço
        uint32_t chunk = 4096u - (uint32_t)(v & 0xFFFULL);
        if (chunk > bytes) chunk = bytes;
        if (n >= MAX_PRD) return -1;
        uint32_t* e = (uint32_t*)(prdt + (uint32_t)n * 16u);
        e[0] = (uint32_t)phys;
        e[1] = 0;
        e[2] = 0;
        e[3] = chunk - 1u;                           // byte count - 1 (par: setores de 512)
        n++;
        v += chunk;
        bytes -= chunk;
    }
    return n;
}

static BlockStatus port_cmd(AhciPort* p, uint8_t cmd, uint64_t lba, uint32_t count,
                            uint64_t buf, uint32_t bytes, bool write) {
    volatile uint8_t* r = p->regs;
    if (wait_clear(r, P_TFD, TFD_BSY | TFD_DRQ, TIMEOUT_IO)) {
        port_recover(p);
        return BLOCK_ERR_TIMEOUT;
    }

    int nprd = 0;
    if (bytes) {
        nprd = build_prdt(p->ctba, buf, bytes);
        if (nprd < 0) return BLOCK_ERR_INVALID;
    }

    uint32_t* h = (uint32_t*)p->clb;                // cabeçalho do slot 0
    h[0] = 5u | (write ? (1u << 6) : 0u) | ((uint32_t)nprd << 16);   // CFL=5 dwords
    h[1] = 0;                                       // PRDBC
    h[2] = (uint32_t)(uintptr_t)p->ctba;
    h[3] = 0;

    uint8_t* f = p->ctba;                           // Command FIS (H2D)
    for (int i = 0; i < 64; i++) f[i] = 0;
    f[0]  = 0x27;
    f[1]  = 0x80;                                   // bit C: é um comando
    f[2]  = cmd;
    f[7]  = 0x40;                                   // modo LBA
    f[4]  = (uint8_t)(lba);        f[5]  = (uint8_t)(lba >> 8);   f[6]  = (uint8_t)(lba >> 16);
    f[8]  = (uint8_t)(lba >> 24);  f[9]  = (uint8_t)(lba >> 32);  f[10] = (uint8_t)(lba >> 40);
    f[12] = (uint8_t)count;        f[13] = (uint8_t)(count >> 8);

    wr32(r, P_IS, 0xFFFFFFFFu);
    wr32(r, P_SERR, 0xFFFFFFFFu);
    full_barrier();
    wr32(r, P_CI, 1u);

    uint64_t deadline = rdtsc() + TIMEOUT_IO;
    while (rd32(r, P_CI) & 1u) {
        if (rd32(r, P_IS) & IS_TFES) { port_recover(p); return BLOCK_ERR_IO; }
        if (rdtsc() > deadline)      { port_recover(p); return BLOCK_ERR_TIMEOUT; }
        __asm__ volatile ("pause");
    }
    if ((rd32(r, P_IS) & IS_TFES) || (rd32(r, P_TFD) & (TFD_ERR | TFD_DF))) {
        port_recover(p);
        return BLOCK_ERR_IO;
    }
    return BLOCK_OK;
}

static BlockStatus transfer(BlockDevice* d, uint64_t lba, uint32_t count,
                            void* buffer, bool write) {
    if (!d || !d->ready || !buffer || !count || lba >= d->sector_count ||
        (uint64_t)count > d->sector_count - lba)
        return BLOCK_ERR_INVALID;
    AhciPort* p = (AhciPort*)d->driver_data;

    uint8_t* cur = (uint8_t*)buffer;
    while (count) {
        uint32_t n = count < MAX_SECTORS_CMD ? count : MAX_SECTORS_CMD;
        BlockStatus st;
        if ((uintptr_t)cur & 1u) {
            // DMA exige endereço par: usa a página de bounce (8 setores por vez).
            if (n > 8) n = 8;
            uint32_t bytes = n * BLOCK_SECTOR_SIZE;
            if (write) for (uint32_t i = 0; i < bytes; i++) p->bounce[i] = cur[i];
            st = port_cmd(p, write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT, lba, n,
                          (uint64_t)(uintptr_t)p->bounce, bytes, write);
            if (st == BLOCK_OK && !write) for (uint32_t i = 0; i < bytes; i++) cur[i] = p->bounce[i];
        } else {
            st = port_cmd(p, write ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT, lba, n,
                          (uint64_t)(uintptr_t)cur, n * BLOCK_SECTOR_SIZE, write);
        }
        if (st != BLOCK_OK) return st;
        cur += (uint64_t)n * BLOCK_SECTOR_SIZE;
        lba += n;
        count -= n;
    }
    return BLOCK_OK;
}

static BlockStatus ahci_read(BlockDevice* d, uint64_t lba, uint32_t count, void* buffer) {
    return transfer(d, lba, count, buffer, false);
}
static BlockStatus ahci_write(BlockDevice* d, uint64_t lba, uint32_t count, const void* buffer) {
    return transfer(d, lba, count, (void*)buffer, true);
}
static BlockStatus ahci_flush(BlockDevice* d) {
    if (!d || !d->ready) return BLOCK_ERR_INVALID;
    return port_cmd((AhciPort*)d->driver_data, ATA_FLUSH_EXT, 0, 0, 0, 0, false);
}

// Aloca e liga uma porta; IDENTIFY preenche o BlockDevice. true = disco usável.
static bool port_setup(AhciPort* p, volatile uint8_t* regs) {
    p->regs = regs;
    if (port_stop(regs)) return false;

    uint8_t* a = (uint8_t*)pfa_alloc_frame();     // Command List (1K) + FIS (256, em +1024)
    uint8_t* b = (uint8_t*)pfa_alloc_frame();     // Command Table
    uint8_t* c = (uint8_t*)pfa_alloc_frame();     // bounce
    if (!a || !b || !c) return false;             // (frames perdidos: só no boot, aceitável)
    zero_page(a); zero_page(b); zero_page(c);
    p->clb = a; p->ctba = b; p->bounce = c;

    wr32(regs, P_CLB,  (uint32_t)(uintptr_t)a);
    wr32(regs, P_CLBU, 0);
    wr32(regs, P_FB,   (uint32_t)(uintptr_t)(a + 1024));
    wr32(regs, P_FBU,  0);
    wr32(regs, P_IE, 0);                          // sem interrupções: polling
    wr32(regs, P_SERR, 0xFFFFFFFFu);
    wr32(regs, P_IS, 0xFFFFFFFFu);
    if (port_start(regs)) return false;

    // IDENTIFY
    uint16_t* id = (uint16_t*)kzalloc(512);
    if (!id) return false;
    BlockStatus st = port_cmd(p, ATA_IDENTIFY, 0, 0, (uint64_t)(uintptr_t)id, 512, false);
    bool ok = false;
    if (st == BLOCK_OK) {
        uint64_t sectors = 0;
        if (id[83] & (1u << 10))                  // 48-bit LBA
            sectors = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                      ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
        else
            sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);

        uint32_t logical = 512;
        if ((id[106] & 0xC000u) == 0x4000u && (id[106] & (1u << 12)))
            logical = (((uint32_t)id[118] << 16) | id[117]) * 2u;

        if (sectors && logical == 512) {          // só setor lógico de 512 B
            p->dev.sector_size  = BLOCK_SECTOR_SIZE;
            p->dev.sector_count = sectors;
            p->dev.read  = ahci_read;
            p->dev.write = ahci_write;
            p->dev.flush = ahci_flush;
            p->dev.driver_data = p;
            p->dev.ready = true;
            ok = true;
        }
    }
    kfree(id);
    return ok;
}

int ahci_init(void) {
    g_count = 0;
    PciDevice d;
    if (!pci_find_class(0x01, 0x06, 0x01, 0, &d)) return 0;

    uint32_t bar = pci_read32(d.bus, d.dev, d.fn, 0x24);          // BAR5 = ABAR
    if (bar & 1u) return 0;                                       // I/O: não é AHCI
    if (((bar >> 1) & 3u) == 2u && pci_read32(d.bus, d.dev, d.fn, 0x28) != 0)
        return 0;                                                 // acima de 4 GiB: não mapeado
    uint64_t base = bar & ~0xFULL;
    if (!base) return 0;
    pci_enable_mem_dma(&d);

    // O identity-map do kernel é "write-back". Registradores de dispositivo
    // PRECISAM de página sem cache (PCD|PWT), senão em hardware real as
    // leituras ficam presas no cache.
    for (uint64_t a = base & ~0xFFFULL; a < base + 0x1100ULL; a += 0x1000ULL)
        if (!paging_map(a, a, PAGE_WRITABLE | PTE_PCD | PTE_PWT)) return 0;
    g_hba = (volatile uint8_t*)(uintptr_t)base;

    // Se a BIOS ainda é "dona" do controlador, pede a posse (handoff).
    if (rd32(g_hba, HBA_CAP2) & 1u) {
        wr32(g_hba, HBA_BOHC, rd32(g_hba, HBA_BOHC) | 2u);
        wait_clear(g_hba, HBA_BOHC, 1u, TIMEOUT_STOP);            // BOS limpa (melhor esforço)
    }
    wr32(g_hba, HBA_GHC, rd32(g_hba, HBA_GHC) | GHC_AE);

    uint32_t pi = rd32(g_hba, HBA_PI);
    for (uint32_t i = 0; i < 32 && g_count < AHCI_MAX_DEVICES; i++) {
        if (!(pi & (1u << i))) continue;
        volatile uint8_t* regs = g_hba + 0x100u + i * 0x80u;
        uint32_t ssts = rd32(regs, P_SSTS);
        if ((ssts & 0xFu) != 3u || ((ssts >> 8) & 0xFu) != 1u) continue;   // sem link ativo
        if (rd32(regs, P_SIG) != SIG_ATA) continue;                        // ATAPI etc.: ignora
        AhciPort* p = &g_ports[g_count];
        if (port_setup(p, regs)) g_count++;
    }
    return g_count;
}

BlockDevice* ahci_device(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_ports[index].dev;
}
