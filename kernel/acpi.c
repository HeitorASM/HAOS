#include "acpi.h"
#include "multiboot2.h"

#define PHYS_LIMIT 0x100000000ULL      // só enxergamos os 4 GiB com identity-map

static bool     g_have_fadt;
static bool     g_have_s5;
static uint16_t g_pm1a_cnt, g_pm1b_cnt;
static uint8_t  g_slp_a, g_slp_b;
static uint32_t g_smi_cmd;
static uint8_t  g_acpi_enable;
static bool     g_reset_ok;            // FADT tem registrador de reset válido
static uint8_t  g_reset_space;         // 0 = memória, 1 = I/O
static uint64_t g_reset_addr;
static uint8_t  g_reset_value;

// ---- utilidades ----
static bool checksum_ok(const uint8_t* p, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static bool sig_is(const uint8_t* p, const char* s) {
    return p[0] == (uint8_t)s[0] && p[1] == (uint8_t)s[1] && p[2] == (uint8_t)s[2] && p[3] == (uint8_t)s[3];
}

// Valida um cabeçalho de tabela SDT em `phys` e devolve o ponteiro (ou NULL).
static const uint8_t* map_table(uint64_t phys) {
    if (!phys || phys + 36 > PHYS_LIMIT) return NULL;
    const uint8_t* t = (const uint8_t*)(uintptr_t)phys;
    uint32_t len = rd32(t + 4);
    if (len < 36 || len > 0x1000000u || phys + len > PHYS_LIMIT) return NULL;
    if (!checksum_ok(t, len)) return NULL;
    return t;
}

// ---- \_S5 dentro do DSDT (AML) ----
// Padrão: [\] _S5_ PackageOp(0x12) PkgLength NumElements elem0 elem1 ...
static bool aml_const(const uint8_t* aml, uint32_t len, uint32_t* p, uint8_t* out) {
    if (*p >= len) return false;
    uint8_t op = aml[*p];
    if (op == 0x00 || op == 0x01) { *out = op; (*p)++; return true; }             // ZeroOp / OneOp
    if (op == 0x0A && *p + 2 <= len) { *out = aml[*p + 1]; *p += 2; return true; } // BytePrefix
    if (op == 0x0B && *p + 3 <= len) { *out = aml[*p + 1]; *p += 3; return true; } // WordPrefix
    if (op == 0x0C && *p + 5 <= len) { *out = aml[*p + 1]; *p += 5; return true; } // DWordPrefix
    return false;
}

static bool parse_s5(const uint8_t* aml, uint32_t len, uint8_t* a, uint8_t* b) {
    for (uint32_t i = 2; i + 8 < len; i++) {
        if (!(aml[i] == '_' && aml[i+1] == 'S' && aml[i+2] == '5' && aml[i+3] == '_')) continue;
        bool named = aml[i-1] == 0x08 || (aml[i-1] == 0x5C && i >= 2 && aml[i-2] == 0x08);
        if (!named) continue;
        uint32_t p = i + 4;
        if (aml[p] != 0x12) continue;                  // PackageOp
        p++;
        p += 1u + (aml[p] >> 6);                       // PkgLength (1 a 4 bytes)
        if (p >= len) return false;
        p++;                                           // NumElements
        if (aml_const(aml, len, &p, a) && aml_const(aml, len, &p, b)) return true;
        return false;
    }
    return false;
}

// ---- FADT ----
static void read_fadt(const uint8_t* f) {
    uint32_t len = rd32(f + 4);
    if (len < 116) return;                             // curta demais (ACPI 1.0 tem 116+)
    g_have_fadt = true;
    g_smi_cmd     = rd32(f + 48);
    g_acpi_enable = f[52];
    g_pm1a_cnt    = (uint16_t)rd32(f + 64);
    g_pm1b_cnt    = (uint16_t)rd32(f + 68);

    uint32_t flags = rd32(f + 112);
    if (len >= 129 && (flags & (1u << 10))) {          // RESET_REG_SUP
        g_reset_space = f[116];
        g_reset_addr  = rd64(f + 120);
        g_reset_value = f[128];
        g_reset_ok = (g_reset_space == 0 || g_reset_space == 1) && g_reset_addr != 0;
    }

    // Liga o modo ACPI se a BIOS deixou desligado (SCI_EN = bit 0 de PM1a_CNT).
    if (g_pm1a_cnt && !(inw(g_pm1a_cnt) & 1u) && g_smi_cmd && g_acpi_enable) {
        outb((uint16_t)g_smi_cmd, g_acpi_enable);
        for (uint32_t i = 0; i < 3000000u && !(inw(g_pm1a_cnt) & 1u); i++) io_wait();
    }

    uint64_t dsdt = rd32(f + 40);
    if (len >= 148) {
        uint64_t x = rd64(f + 140);
        if (x) dsdt = x;
    }
    const uint8_t* d = map_table(dsdt);
    if (d && sig_is(d, "DSDT") && g_pm1a_cnt) {
        uint8_t a = 0, b = 0;
        uint32_t dl = rd32(d + 4);
        if (parse_s5(d + 36, dl - 36, &a, &b)) { g_slp_a = a; g_slp_b = b; g_have_s5 = true; }
    }
}

static void walk_root(const uint8_t* root, bool xsdt) {
    uint32_t len = rd32(root + 4);
    uint32_t esz = xsdt ? 8 : 4;
    for (uint32_t off = 36; off + esz <= len; off += esz) {
        uint64_t addr = xsdt ? rd64(root + off) : rd32(root + off);
        const uint8_t* t = map_table(addr);
        if (t && sig_is(t, "FACP")) { read_fadt(t); return; }
    }
}

static bool use_rsdp(const uint8_t* r) {
    if (!(r[0]=='R' && r[1]=='S' && r[2]=='D' && r[3]==' ' && r[4]=='P' && r[5]=='T' && r[6]=='R' && r[7]==' '))
        return false;
    if (!checksum_ok(r, 20)) return false;
    uint8_t rev = r[15];
    if (rev >= 2 && checksum_ok(r, 36)) {
        uint64_t x = rd64(r + 24);
        const uint8_t* t = map_table(x);
        if (t && sig_is(t, "XSDT")) { walk_root(t, true); if (g_have_fadt) return true; }
    }
    const uint8_t* t = map_table(rd32(r + 16));
    if (t && sig_is(t, "RSDT")) walk_root(t, false);
    return g_have_fadt;
}

static const uint8_t* scan_rsdp(uint32_t start, uint32_t end) {
    for (uint32_t a = start; a + 20 <= end; a += 16) {
        const uint8_t* p = (const uint8_t*)(uintptr_t)a;
        if (p[0]=='R' && p[1]=='S' && p[2]=='D' && p[3]==' ' && checksum_ok(p, 20)) return p;
    }
    return NULL;
}

void acpi_init(uint32_t mb2_info) {
    g_have_fadt = g_have_s5 = g_reset_ok = false;

    // 1) RSDP entregue pelo bootloader (funciona também com UEFI+GRUB).
    if (mb2_info) {
        const uint8_t* ptr = (const uint8_t*)(uintptr_t)(mb2_info + 8);
        const mb2_tag_t* tag = (const mb2_tag_t*)ptr;
        while (tag->type != MB2_TAG_END) {
            if ((tag->type == MB2_TAG_ACPI_NEW || tag->type == MB2_TAG_ACPI_OLD) && tag->size >= 8 + 20) {
                if (use_rsdp((const uint8_t*)tag + 8)) return;
            }
            uint32_t sz = (tag->size + 7u) & ~7u;
            if (sz < 8) break;
            tag = (const mb2_tag_t*)((const uint8_t*)tag + sz);
        }
    }
    // 2) BIOS legado: 1 KiB do EBDA e a área 0xE0000-0xFFFFF.
    uint32_t ebda = (uint32_t)(*(volatile const uint16_t*)(uintptr_t)0x40E) << 4;
    const uint8_t* r = NULL;
    if (ebda >= 0x80000 && ebda < 0xA0000) r = scan_rsdp(ebda, ebda + 1024);
    if (!r) r = scan_rsdp(0xE0000, 0x100000);
    if (r) use_rsdp(r);
}

bool acpi_available(void)    { return g_have_fadt; }
bool acpi_can_poweroff(void) { return g_have_fadt && g_have_s5; }

void acpi_poweroff(void) {
    __asm__ volatile ("cli");
    if (g_have_fadt && g_have_s5 && g_pm1a_cnt) {
        uint16_t cur = inw(g_pm1a_cnt);
        outw(g_pm1a_cnt, (uint16_t)((cur & ~(7u << 10)) | ((uint16_t)g_slp_a << 10) | (1u << 13)));
        if (g_pm1b_cnt) {
            uint16_t curb = inw(g_pm1b_cnt);
            outw(g_pm1b_cnt, (uint16_t)((curb & ~(7u << 10)) | ((uint16_t)g_slp_b << 10) | (1u << 13)));
        }
        for (uint32_t i = 0; i < 5000000u; i++) io_wait();     // dá tempo de a placa cortar a energia
    } else if (!g_have_fadt) {
        // Sem ACPI (VMs simples): portas conhecidas de "power off".
        outw(0x604, 0x2000);     // QEMU
        outw(0xB004, 0x2000);    // Bochs / QEMU antigo
        outw(0x4004, 0x3400);    // VirtualBox
    }
    for (;;) __asm__ volatile ("hlt");
}

static void kbd_wait_input_empty(void) {
    for (uint32_t i = 0; i < 100000u && (inb(0x64) & 0x02); i++) io_wait();
}

void acpi_reboot(void) {
    __asm__ volatile ("cli");

    // 1) Registrador de reset do ACPI (o método "oficial" em PCs modernos).
    if (g_reset_ok) {
        if (g_reset_space == 1) {
            outb((uint16_t)g_reset_addr, g_reset_value);
        } else if (g_reset_addr < PHYS_LIMIT) {
            *(volatile uint8_t*)(uintptr_t)g_reset_addr = g_reset_value;
        }
        for (uint32_t i = 0; i < 1000000u; i++) io_wait();
    }
    // 2) Pulso de reset pelo controlador 8042 (teclado).
    kbd_wait_input_empty();
    outb(0x64, 0xFE);
    for (uint32_t i = 0; i < 1000000u; i++) io_wait();
    // 3) Porta de reset do chipset (0xCF9).
    outb(0xCF9, 0x02);
    io_wait();
    outb(0xCF9, 0x06);
    for (uint32_t i = 0; i < 1000000u; i++) io_wait();
    // 4) Último recurso: triple fault (IDT vazia + exceção).
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idt));
    for (;;) __asm__ volatile ("hlt");
}
