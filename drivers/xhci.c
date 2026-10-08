// drivers/xhci.c — xHCI por polling: um comando por vez, sem interrupções.
//
// Fluxo: PCI (classe 0C:03:30) -> BAR0 mapeado SEM cache -> toma posse da
// BIOS (USB Legacy Support) -> reset -> DCBAA/anéis de comando e evento ->
// RUN -> para cada porta conectada: reset, Enable Slot, Address Device,
// descritores, SET_CONFIGURATION, SET_PROTOCOL(boot), Configure Endpoint e
// um TRB de interrupção sempre enfileirado. Cada relatório vira
// scancode/movimento via usb_hid.c.
#include "xhci.h"
#include "pci.h"
#include "usb_hid.h"
#include "../kernel/memory.h"
#include "../kernel/paging.h"

#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)

#define MAP_BYTES    0x20000u          // 128 KiB de registradores
#define RING_TRBS    256u
#define MAX_SLOTS_EN 8u
#define TIMEOUT      2000000000ULL     // ciclos de TSC (~0,7 s)

// ---- registradores operacionais ----
#define OP_USBCMD 0x00
#define OP_USBSTS 0x04
#define OP_CRCR   0x18
#define OP_DCBAAP 0x30
#define OP_CONFIG 0x38
#define OP_PORTSC 0x400
#define CMD_RS    (1u << 0)
#define CMD_HCRST (1u << 1)
#define STS_HCH   (1u << 0)
#define STS_CNR   (1u << 11)

// PORTSC: bits "write-1-to-clear"/de ação que NUNCA devem voltar escritos por acidente
#define PORTSC_PR    (1u << 4)
#define PORTSC_PP    (1u << 9)
#define PORTSC_CHG   (0x7Fu << 17)     // CSC PEC WRC OCC PRC PLC CEC
#define PORTSC_NOKEEP (PORTSC_CHG | (1u << 1) | PORTSC_PR | (1u << 16) | (1u << 31))

typedef struct { uint32_t d[4]; } Trb;
typedef struct { Trb* trb; uint32_t enq; uint32_t size; uint32_t pcs; } Ring;

typedef struct {
    bool     used;
    uint8_t  slot, port, speed;
    uint8_t  kind;                     // 1 = teclado, 2 = mouse
    uint8_t  dci;                      // endpoint de interrupção
    uint16_t report_len;
    uint8_t* in_ctx; uint8_t* out_ctx; uint8_t* buf; uint8_t* ep0_pg; uint8_t* hid_pg;
    Ring     ep0, hid;
    volatile int     ctl_done;
    volatile uint8_t ctl_code;
    uint64_t ctl_wait;                 // endereço do TRB de status esperado
    uint8_t  errs;
} XDev;

static volatile uint8_t* g_cap;
static volatile uint8_t* g_op;
static volatile uint8_t* g_rt;
static volatile uint8_t* g_db;
static uint32_t g_ctx;                 // tamanho do contexto: 32 ou 64 bytes
static uint32_t g_maxports;
static Ring     g_cmd;
static volatile uint32_t* g_evt;       // anel de eventos (RING_TRBS * 4 dwords)
static uint32_t g_evt_deq, g_evt_ccs;
static uint64_t* g_dcbaa;
static bool     g_ready;
static int      g_poll_lock;
static volatile int g_port_change;
static XDev     g_dev[MAX_SLOTS_EN + 1];
static uint8_t  g_port_slot[256];
static uint8_t  g_port_failed[256];

static struct { volatile int done; volatile uint8_t code; volatile uint8_t slot; } g_cmdres;

extern volatile uint64_t timer_ticks;

// ---- utilidades ----
static inline uint32_t rd32(volatile uint8_t* b, uint32_t off) { return *(volatile uint32_t*)(b + off); }
static inline void wr32(volatile uint8_t* b, uint32_t off, uint32_t v) { *(volatile uint32_t*)(b + off) = v; }
static inline void wr64(volatile uint8_t* b, uint32_t off, uint64_t v) {
    wr32(b, off, (uint32_t)v);
    wr32(b, off + 4, (uint32_t)(v >> 32));
}
static inline void full_barrier(void) { __asm__ volatile ("mfence" ::: "memory"); }
static inline uint64_t phys(const void* p) { return (uint64_t)(uintptr_t)p; }

static void delay_ms(uint32_t ms) {
    uint64_t t_end = timer_ticks + (ms + 9u) / 10u + 1u;
    uint64_t tsc_end = rdtsc() + (uint64_t)ms * 10000000ULL;   // trava de segurança
    while (timer_ticks < t_end && rdtsc() < tsc_end) __asm__ volatile ("pause");
}

static bool wait_op(uint32_t off, uint32_t mask, uint32_t want) {
    uint64_t dl = rdtsc() + TIMEOUT;
    while ((rd32(g_op, off) & mask) != want) {
        if (rdtsc() > dl) return false;
        __asm__ volatile ("pause");
    }
    return true;
}

static void zero_page(void* p) {
    uint64_t* q = (uint64_t*)p;
    for (int i = 0; i < 512; i++) q[i] = 0;
}
static uint8_t* dma_page(void) {
    uint8_t* p = (uint8_t*)(uintptr_t)pfa_alloc_frame();
    if (p) zero_page(p);
    return p;
}

// ---- anéis ----
static void ring_init(Ring* r, void* page) {
    r->trb = (Trb*)page; r->enq = 0; r->size = RING_TRBS; r->pcs = 1;
}

// Enfileira um TRB (o bit de ciclo vai por último) e devolve o endereço dele.
static uint64_t ring_push(Ring* r, uint64_t param, uint32_t status, uint32_t ctrl) {
    volatile uint32_t* t = r->trb[r->enq].d;
    uint64_t at = phys(&r->trb[r->enq]);
    t[0] = (uint32_t)param;
    t[1] = (uint32_t)(param >> 32);
    t[2] = status;
    full_barrier();
    t[3] = ctrl | r->pcs;
    r->enq++;
    if (r->enq == r->size - 1) {              // último = Link TRB de volta ao início
        volatile uint32_t* l = r->trb[r->enq].d;
        uint64_t base = phys(r->trb);
        l[0] = (uint32_t)base;
        l[1] = (uint32_t)(base >> 32);
        l[2] = 0;
        full_barrier();
        l[3] = (6u << 10) | (1u << 1) | r->pcs;   // Link + Toggle Cycle
        r->enq = 0;
        r->pcs ^= 1u;
    }
    return at;
}

// ---- eventos ----
static void hid_requeue(XDev* d) {
    ring_push(&d->hid, phys(d->buf + 2048), d->report_len, (1u << 10) | (1u << 5) | (1u << 2));
    full_barrier();
    wr32(g_db, (uint32_t)d->slot * 4u, d->dci);
}

static void handle_event(volatile uint32_t* e) {
    uint32_t type = (e[3] >> 10) & 0x3Fu;
    if (type == 33) {                                   // Command Completion
        g_cmdres.code = (uint8_t)(e[2] >> 24);
        g_cmdres.slot = (uint8_t)(e[3] >> 24);
        g_cmdres.done = 1;
    } else if (type == 32) {                            // Transfer Event
        uint32_t slot = e[3] >> 24, ep = (e[3] >> 16) & 0x1Fu;
        uint8_t  code = (uint8_t)(e[2] >> 24);
        uint32_t resid = e[2] & 0xFFFFFFu;
        uint64_t ptr = (uint64_t)e[0] | ((uint64_t)e[1] << 32);
        if (slot == 0 || slot > MAX_SLOTS_EN) return;
        XDev* d = &g_dev[slot];
        if (!d->used) return;
        if (ep == 1) {                                  // controle
            if ((code != 1 && code != 13) || ptr == d->ctl_wait) {
                d->ctl_code = code;
                d->ctl_done = 1;
            }
        } else if (ep == d->dci) {                      // relatório HID
            if (code == 1 || code == 13) {
                uint32_t n = d->report_len > resid ? d->report_len - resid : 0;
                if (d->kind == 1) usb_hid_keyboard_report(d->buf + 2048, n);
                else              usb_hid_mouse_report(d->buf + 2048, n);
                d->errs = 0;
                hid_requeue(d);
            } else if (d->errs < 5) {
                d->errs++;
                hid_requeue(d);
            }
        }
    } else if (type == 34) {                            // Port Status Change
        g_port_change = 1;
    }
}

static void evt_poll(void) {
    bool any = false;
    for (;;) {
        volatile uint32_t* e = g_evt + g_evt_deq * 4u;
        if ((e[3] & 1u) != g_evt_ccs) break;
        full_barrier();
        handle_event(e);
        any = true;
        if (++g_evt_deq == RING_TRBS) { g_evt_deq = 0; g_evt_ccs ^= 1u; }
    }
    if (any)   // EHB (bit 3) = "já li até aqui"
        wr64(g_rt, 0x38, phys((const void*)(g_evt + g_evt_deq * 4u)) | 8u);
}

// ---- comandos ----
static bool xhci_cmd(uint64_t param, uint32_t ctrl, uint8_t* slot_out) {
    g_cmdres.done = 0;
    ring_push(&g_cmd, param, 0, ctrl);
    full_barrier();
    wr32(g_db, 0, 0);
    uint64_t dl = rdtsc() + TIMEOUT;
    while (!g_cmdres.done) {
        evt_poll();
        if (rdtsc() > dl) return false;
        __asm__ volatile ("pause");
    }
    if (slot_out) *slot_out = g_cmdres.slot;
    return g_cmdres.code == 1;
}

// ---- transferência de controle no EP0 ----
static bool ctl_xfer(XDev* d, uint8_t bm, uint8_t req, uint16_t val, uint16_t idx,
                     uint16_t len, bool in) {
    d->ctl_done = 0;
    uint64_t setup = (uint64_t)bm | ((uint64_t)req << 8) | ((uint64_t)val << 16) |
                     ((uint64_t)idx << 32) | ((uint64_t)len << 48);
    uint32_t trt = len ? (in ? 3u : 2u) : 0u;
    ring_push(&d->ep0, setup, 8, (2u << 10) | (1u << 6) | (trt << 16));      // Setup (IDT)
    if (len)
        ring_push(&d->ep0, phys(d->buf), len, (3u << 10) | (in ? (1u << 16) : 0u));   // Data
    uint32_t sdir = (len && in) ? 0u : (1u << 16);
    d->ctl_wait = ring_push(&d->ep0, 0, 0, (4u << 10) | (1u << 5) | sdir);    // Status + IOC
    full_barrier();
    wr32(g_db, (uint32_t)d->slot * 4u, 1);

    uint64_t dl = rdtsc() + TIMEOUT;
    while (!d->ctl_done) {
        evt_poll();
        if (rdtsc() > dl) return false;
        __asm__ volatile ("pause");
    }
    return d->ctl_code == 1 || d->ctl_code == 13;
}

static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ---- portas ----
static uint32_t port_off(uint32_t port) { return OP_PORTSC + (port - 1u) * 0x10u; }

static bool reset_port(uint32_t port, uint8_t* speed) {
    uint32_t off = port_off(port);
    uint32_t ps = rd32(g_op, off);
    if (!(ps & 1u)) return false;
    if (!(ps & 2u)) {                                   // não habilitada: reseta
        wr32(g_op, off, (ps & ~PORTSC_NOKEEP) | PORTSC_PR);
        uint64_t dl = rdtsc() + TIMEOUT;
        while (!(rd32(g_op, off) & (1u << 21))) {       // PRC
            if (rdtsc() > dl) return false;
            __asm__ volatile ("pause");
        }
        ps = rd32(g_op, off);
        wr32(g_op, off, (ps & ~PORTSC_NOKEEP) | (1u << 21));
        delay_ms(20);                                   // tempo de recuperação do reset
    }
    ps = rd32(g_op, off);
    if (!(ps & 1u) || !(ps & 2u)) return false;
    *speed = (uint8_t)((ps >> 10) & 0xFu);
    return *speed >= 1 && *speed <= 6;
}

static void dev_release(XDev* d) {
    if (d->kind == 1) usb_hid_keyboard_reset();
    d->used = false;
    d->kind = 0;
}

static bool dev_alloc(XDev* d) {
    if (!d->in_ctx) {
        d->in_ctx = dma_page(); d->out_ctx = dma_page(); d->buf = dma_page();
        d->ep0_pg = dma_page(); d->hid_pg = dma_page();
    } else {
        zero_page(d->in_ctx); zero_page(d->out_ctx); zero_page(d->buf);
        zero_page(d->ep0_pg); zero_page(d->hid_pg);
    }
    return d->in_ctx && d->out_ctx && d->buf && d->ep0_pg && d->hid_pg;
}

static uint32_t ilog2_u32(uint32_t v) { uint32_t r = 0; while (v > 1) { v >>= 1; r++; } return r; }

// Descobre a interface HID de boot (teclado/mouse) e seu endpoint de interrupção IN.
static bool parse_config(const uint8_t* c, uint32_t total, uint8_t* cfg_val, uint8_t* iface,
                         uint8_t* kind, uint8_t* ep_addr, uint16_t* ep_mps, uint8_t* ep_interval) {
    if (total < 9) return false;
    *cfg_val = c[5];
    bool in_cand = false;
    for (uint32_t p = 0; p + 2 <= total && c[p] >= 2; p += c[p]) {
        if (p + c[p] > total) break;
        if (c[p + 1] == 4 && c[p] >= 9) {                       // Interface
            in_cand = (c[p + 5] == 3 && c[p + 6] == 1 && (c[p + 7] == 1 || c[p + 7] == 2));
            if (in_cand) { *iface = c[p + 2]; *kind = c[p + 7]; }
        } else if (c[p + 1] == 5 && c[p] >= 7 && in_cand) {      // Endpoint
            if ((c[p + 3] & 3u) == 3u && (c[p + 2] & 0x80u)) {   // Interrupt IN
                *ep_addr = c[p + 2];
                *ep_mps = (uint16_t)(le16(c + p + 4) & 0x7FFu);
                *ep_interval = c[p + 6];
                return true;
            }
        }
    }
    return false;
}

static bool enumerate(uint32_t port) {
    uint8_t speed = 0;
    if (!reset_port(port, &speed)) return false;

    uint8_t slot = 0;
    if (!xhci_cmd(0, 9u << 10, &slot) || slot == 0 || slot > MAX_SLOTS_EN) return false;
    XDev* d = &g_dev[slot];
    if (!dev_alloc(d)) { xhci_cmd(0, (10u << 10) | ((uint32_t)slot << 24), NULL); return false; }
    d->used = true; d->slot = slot; d->port = (uint8_t)port; d->speed = speed;
    d->kind = 0; d->errs = 0;
    ring_init(&d->ep0, d->ep0_pg);
    ring_init(&d->hid, d->hid_pg);

    uint16_t mps0 = speed == 2 ? 8 : speed == 1 ? 8 : speed == 3 ? 64 : 512;

    // Address Device
    uint32_t* icc = (uint32_t*)d->in_ctx;
    icc[1] = (1u << 0) | (1u << 1);                                  // A0 (slot) + A1 (EP0)
    uint32_t* sc = (uint32_t*)(d->in_ctx + g_ctx);
    sc[0] = ((uint32_t)speed << 20) | (1u << 27);                    // 1 entrada de contexto
    sc[1] = (uint32_t)port << 16;                                    // root hub port
    uint32_t* ep = (uint32_t*)(d->in_ctx + 2u * g_ctx);
    ep[1] = (3u << 1) | (4u << 3) | ((uint32_t)mps0 << 16);          // CErr=3, Control, MPS
    uint64_t deq = phys(d->ep0.trb) | 1u;
    ep[2] = (uint32_t)deq; ep[3] = (uint32_t)(deq >> 32);
    ep[4] = 8;
    g_dcbaa[slot] = phys(d->out_ctx);
    full_barrier();
    if (!xhci_cmd(phys(d->in_ctx), (11u << 10) | ((uint32_t)slot << 24), NULL)) goto fail;

    // Primeiros 8 bytes do descritor de dispositivo: bMaxPacketSize0
    if (!ctl_xfer(d, 0x80, 6, 0x0100, 0, 8, true)) goto fail;
    if (speed == 1 && d->buf[7] != 8 && (d->buf[7] == 16 || d->buf[7] == 32 || d->buf[7] == 64)) {
        zero_page(d->in_ctx);
        icc[1] = 1u << 1;
        ep[1] = (3u << 1) | (4u << 3) | ((uint32_t)d->buf[7] << 16);
        if (!xhci_cmd(phys(d->in_ctx), (13u << 10) | ((uint32_t)slot << 24), NULL)) goto fail;
    }

    // Descritor de configuração (9 bytes -> wTotalLength -> inteiro)
    if (!ctl_xfer(d, 0x80, 6, 0x0200, 0, 9, true)) goto fail;
    uint16_t total = le16(d->buf + 2);
    if (total < 9) goto fail;
    if (total > 512) total = 512;
    if (!ctl_xfer(d, 0x80, 6, 0x0200, 0, total, true)) goto fail;

    uint8_t cfg_val = 0, iface = 0, kind = 0, ep_addr = 0, interval = 0;
    uint16_t ep_mps = 0;
    if (!parse_config(d->buf, total, &cfg_val, &iface, &kind, &ep_addr, &ep_mps, &interval)) goto fail;

    if (!ctl_xfer(d, 0x00, 9, cfg_val, 0, 0, false)) goto fail;      // SET_CONFIGURATION
    ctl_xfer(d, 0x21, 0x0B, 0, iface, 0, false);                     // SET_PROTOCOL(boot) — pode falhar

    // Configure Endpoint: endpoint de interrupção IN
    uint32_t dci = (uint32_t)(ep_addr & 0xFu) * 2u + 1u;
    uint32_t exp;
    if (speed == 1 || speed == 2) {                                  // FS/LS: bInterval em ms
        uint32_t ms = interval ? interval : 1u;
        exp = ilog2_u32(ms * 8u);
        if (exp < 3) exp = 3;
        if (exp > 10) exp = 10;
    } else {                                                         // HS/SS: 2^(bInterval-1) x 125us
        exp = interval ? (uint32_t)interval - 1u : 0u;
        if (exp > 15) exp = 15;
    }
    uint16_t rlen = ep_mps ? ep_mps : 8;
    if (rlen > 64) rlen = 64;

    zero_page(d->in_ctx);
    icc = (uint32_t*)d->in_ctx;
    icc[1] = (1u << 0) | (1u << dci);
    sc = (uint32_t*)(d->in_ctx + g_ctx);
    {   // contexto de slot atual (saída) como ponto de partida, com mais entradas
        const uint32_t* out = (const uint32_t*)d->out_ctx;
        for (uint32_t i = 0; i < 8; i++) sc[i] = out[i];
        sc[0] = (sc[0] & 0x07FFFFFFu) | (dci << 27);
    }
    ep = (uint32_t*)(d->in_ctx + (dci + 1u) * g_ctx);
    ep[0] = exp << 16;
    ep[1] = (3u << 1) | (7u << 3) | ((uint32_t)ep_mps << 16);        // Interrupt IN
    deq = phys(d->hid.trb) | 1u;
    ep[2] = (uint32_t)deq; ep[3] = (uint32_t)(deq >> 32);
    ep[4] = (uint32_t)rlen | ((uint32_t)ep_mps << 16);               // avg TRB len | max ESIT payload
    full_barrier();
    if (!xhci_cmd(phys(d->in_ctx), (12u << 10) | ((uint32_t)slot << 24), NULL)) goto fail;

    d->kind = kind;
    d->dci = (uint8_t)dci;
    d->report_len = rlen;
    hid_requeue(d);
    return true;

fail:
    xhci_cmd(0, (10u << 10) | ((uint32_t)slot << 24), NULL);        // Disable Slot
    g_dcbaa[slot] = 0;
    d->used = false;
    return false;
}

static void scan_ports(void) {
    for (uint32_t port = 1; port <= g_maxports && port < 256; port++) {
        uint32_t off = port_off(port);
        uint32_t ps = rd32(g_op, off);
        uint32_t chg = ps & PORTSC_CHG;
        if (chg) wr32(g_op, off, (ps & ~PORTSC_NOKEEP) | chg);       // reconhece mudanças
        if (chg & (1u << 17)) g_port_failed[port] = 0;               // conexão nova: tenta de novo

        bool connected = ps & 1u;
        if (!connected && g_port_slot[port]) {                       // desconectou
            uint8_t slot = g_port_slot[port];
            dev_release(&g_dev[slot]);
            xhci_cmd(0, (10u << 10) | ((uint32_t)slot << 24), NULL);
            g_dcbaa[slot] = 0;
            g_port_slot[port] = 0;
        } else if (connected && !g_port_slot[port] && !g_port_failed[port]) {
            if (enumerate(port)) {
                for (uint32_t s = 1; s <= MAX_SLOTS_EN; s++)
                    if (g_dev[s].used && g_dev[s].port == port) g_port_slot[port] = (uint8_t)s;
            } else {
                g_port_failed[port] = 1;
            }
        }
    }
}

// ---- inicialização ----
static void bios_handoff(uint32_t hccparams1) {
    uint32_t off = ((hccparams1 >> 16) & 0xFFFFu) * 4u;
    for (int guard = 0; off && off < MAP_BYTES - 16u && guard < 64; guard++) {
        uint32_t cap = rd32(g_cap, off);
        if ((cap & 0xFFu) == 1) {                                   // USB Legacy Support
            wr32(g_cap, off, cap | (1u << 24));                     // "o SO é o dono"
            uint64_t dl = rdtsc() + TIMEOUT;
            while ((rd32(g_cap, off) & (1u << 16)) && rdtsc() < dl) __asm__ volatile ("pause");
            if (rd32(g_cap, off) & (1u << 16))                      // BIOS não largou: força
                wr32(g_cap, off, rd32(g_cap, off) & ~(1u << 16));
            uint32_t ctl = rd32(g_cap, off + 4);
            ctl &= ((0x3u << 1) | (0xFFu << 5) | (0x7u << 17));     // desliga SMIs, preserva reservados
            ctl |= (0x7u << 29);                                    // limpa eventos pendentes
            wr32(g_cap, off + 4, ctl);
            return;
        }
        uint32_t next = (cap >> 8) & 0xFFu;
        if (!next) break;
        off += next * 4u;
    }
}

int xhci_init(void) {
    g_ready = false;
    PciDevice pd;
    if (!pci_find_class(0x0C, 0x03, 0x30, 0, &pd)) return 0;

    uint32_t bar = pci_read32(pd.bus, pd.dev, pd.fn, 0x10);
    if (bar & 1u) return 0;
    if (((bar >> 1) & 3u) == 2u && pci_read32(pd.bus, pd.dev, pd.fn, 0x14) != 0) return 0;
    uint64_t base = bar & ~0xFULL;
    if (!base) return 0;
    pci_enable_mem_dma(&pd);

    for (uint64_t a = base & ~0xFFFULL; a < base + MAP_BYTES; a += 0x1000ULL)
        if (!paging_map(a, a, PAGE_WRITABLE | PTE_PCD | PTE_PWT)) return 0;
    g_cap = (volatile uint8_t*)(uintptr_t)base;

    uint32_t caplen = g_cap[0];
    uint32_t hcs1 = rd32(g_cap, 0x04), hcs2 = rd32(g_cap, 0x08), hcc1 = rd32(g_cap, 0x10);
    uint32_t dboff = rd32(g_cap, 0x14) & ~3u, rtsoff = rd32(g_cap, 0x18) & ~0x1Fu;
    uint32_t maxslots = hcs1 & 0xFFu;
    g_maxports = (hcs1 >> 24) & 0xFFu;
    g_ctx = (hcc1 & (1u << 2)) ? 64u : 32u;
    if (caplen < 0x10 || !maxslots || !g_maxports || dboff + 0x400 >= MAP_BYTES ||
        rtsoff + 0x40 >= MAP_BYTES || caplen + OP_PORTSC + g_maxports * 0x10u >= MAP_BYTES)
        return 0;
    g_op = g_cap + caplen;
    g_rt = g_cap + rtsoff;
    g_db = g_cap + dboff;

    bios_handoff(hcc1);

    // Para e reseta o controlador.
    wr32(g_op, OP_USBCMD, rd32(g_op, OP_USBCMD) & ~CMD_RS);
    if (!wait_op(OP_USBSTS, STS_HCH, STS_HCH)) return 0;
    wr32(g_op, OP_USBCMD, CMD_HCRST);
    if (!wait_op(OP_USBCMD, CMD_HCRST, 0)) return 0;
    if (!wait_op(OP_USBSTS, STS_CNR, 0)) return 0;

    uint32_t slots_en = maxslots < MAX_SLOTS_EN ? maxslots : MAX_SLOTS_EN;
    wr32(g_op, OP_CONFIG, slots_en);

    // DCBAA + scratchpads
    g_dcbaa = (uint64_t*)dma_page();
    uint8_t* cmd_pg = dma_page();
    uint8_t* evt_pg = dma_page();
    uint8_t* erst = dma_page();
    if (!g_dcbaa || !cmd_pg || !evt_pg || !erst) return 0;
    uint32_t nscratch = ((hcs2 >> 27) & 0x1Fu) | (((hcs2 >> 21) & 0x1Fu) << 5);
    if (nscratch) {
        if (nscratch > 512) return 0;
        uint64_t* arr = (uint64_t*)dma_page();
        if (!arr) return 0;
        for (uint32_t i = 0; i < nscratch; i++) {
            uint8_t* sp = dma_page();
            if (!sp) return 0;
            arr[i] = phys(sp);
        }
        g_dcbaa[0] = phys(arr);
    }
    wr64(g_op, OP_DCBAAP, phys(g_dcbaa));

    // Anel de comandos
    ring_init(&g_cmd, cmd_pg);
    wr64(g_op, OP_CRCR, phys(cmd_pg) | 1u);                  // RCS = 1

    // Anel de eventos (1 segmento)
    g_evt = (volatile uint32_t*)evt_pg;
    g_evt_deq = 0; g_evt_ccs = 1;
    uint32_t* e = (uint32_t*)erst;
    e[0] = (uint32_t)phys(evt_pg); e[1] = (uint32_t)(phys(evt_pg) >> 32);
    e[2] = RING_TRBS; e[3] = 0;
    wr32(g_rt, 0x28, 1);                                     // ERSTSZ
    wr64(g_rt, 0x38, phys(evt_pg));                          // ERDP
    wr64(g_rt, 0x30, phys(erst));                            // ERSTBA (ativa o anel)

    // RUN
    wr32(g_op, OP_USBCMD, CMD_RS);
    if (!wait_op(OP_USBSTS, STS_HCH, 0)) return 0;
    g_ready = true;

    // Liga a energia das portas (se preciso) e dá tempo para os dispositivos aparecerem.
    for (uint32_t p = 1; p <= g_maxports; p++) {
        uint32_t ps = rd32(g_op, port_off(p));
        if (!(ps & PORTSC_PP)) wr32(g_op, port_off(p), (ps & ~PORTSC_NOKEEP) | PORTSC_PP);
    }
    delay_ms(120);

    scan_ports();
    evt_poll();
    g_port_change = 0;

    int n = 0;
    for (uint32_t s = 1; s <= MAX_SLOTS_EN; s++) if (g_dev[s].used) n++;
    return n;
}

bool xhci_present(void) { return g_ready; }

void xhci_poll(void) {
    if (!g_ready) return;
    if (__sync_lock_test_and_set(&g_poll_lock, 1)) return;     // já em execução (outra task)
    evt_poll();
    if (g_port_change) {
        g_port_change = 0;
        scan_ports();
        evt_poll();
        g_port_change = 0;     // descarta os eventos gerados pelos nossos próprios resets
    }
    usb_hid_tick();
    __sync_lock_release(&g_poll_lock);
}
