// ============================================================
//  sysinfo.cpp — Deteção de Hardware do HAOS
//
//  CPU: instrução CPUID (leafs 0, 1, 0x80000000–0x80000004)
//  RAM: lê do gestor de memória (mem_get_*)
//
// ============================================================

#include "sysinfo.h"
#include "memory.h"

// ============================================================
//  Membros estáticos da classe HardwareInfo
// ============================================================
char     HardwareInfo::s_cpu_brand[49]  = { "Processador Desconhecido" };
char     HardwareInfo::s_cpu_vendor[13] = { "Unknown     " };
uint32_t HardwareInfo::s_cpu_cores      = 1;
bool     HardwareInfo::s_initialized    = false;

// ============================================================
//  Helper: executa CPUID com leaf e subleaf
// ============================================================
static inline void do_cpuid(uint32_t leaf, uint32_t subleaf,
                             uint32_t& eax, uint32_t& ebx,
                             uint32_t& ecx, uint32_t& edx) {
    __asm__ volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(leaf), "c"(subleaf)
    );
}

// ============================================================
//  HardwareInfo::detect_cpu
//  Preenche brand string, vendor e número de núcleos via CPUID
// ============================================================
void HardwareInfo::detect_cpu() {
    uint32_t eax, ebx, ecx, edx;

    // ---- Vendor string (leaf 0) ----
    // EBX:EDX:ECX contêm os 12 caracteres do vendor
    do_cpuid(0, 0, eax, ebx, ecx, edx);
    uint32_t max_basic = eax;

    // Vendor: EBX, EDX, ECX (nesta ordem, pela especificação da Intel/AMD)
    uint32_t vendor_raw[3] = { ebx, edx, ecx };
    for (int i = 0; i < 3; i++) {
        s_cpu_vendor[i*4 + 0] = (char)((vendor_raw[i]      ) & 0xFF);
        s_cpu_vendor[i*4 + 1] = (char)((vendor_raw[i] >>  8) & 0xFF);
        s_cpu_vendor[i*4 + 2] = (char)((vendor_raw[i] >> 16) & 0xFF);
        s_cpu_vendor[i*4 + 3] = (char)((vendor_raw[i] >> 24) & 0xFF);
    }
    s_cpu_vendor[12] = '\0';

    // ---- Número de núcleos lógicos ----
    // Leaf 0xB (x2APIC) é o método moderno; fallback para leaf 1
    s_cpu_cores = 1;
    if (max_basic >= 0xB) {
        do_cpuid(0xB, 1, eax, ebx, ecx, edx);
        uint32_t cores = ebx & 0xFFFF;
        if (cores > 0) s_cpu_cores = cores;
    } else if (max_basic >= 1) {
        do_cpuid(1, 0, eax, ebx, ecx, edx);
        uint32_t cores = (ebx >> 16) & 0xFF;
        if (cores > 0) s_cpu_cores = cores;
    }

    // ---- Brand String (leafs 0x80000002 a 0x80000004) ----
    // Verifica se o processador suporta estas leafs estendidas
    do_cpuid(0x80000000u, 0, eax, ebx, ecx, edx);
    uint32_t max_ext = eax;

    if (max_ext >= 0x80000004u) {
        // Cada leaf devolve 16 bytes (4 registos × 4 bytes)
        // Total: 48 bytes = 48 caracteres + '\0'
        uint32_t brand_regs[12];
        for (int leaf = 0; leaf < 3; leaf++) {
            do_cpuid(0x80000002u + (uint32_t)leaf, 0,
                     brand_regs[leaf*4 + 0],
                     brand_regs[leaf*4 + 1],
                     brand_regs[leaf*4 + 2],
                     brand_regs[leaf*4 + 3]);
        }

        // Converte array de uint32 para string de chars
        char* dst = s_cpu_brand;
        for (int i = 0; i < 12; i++) {
            *dst++ = (char)((brand_regs[i]      ) & 0xFF);
            *dst++ = (char)((brand_regs[i] >>  8) & 0xFF);
            *dst++ = (char)((brand_regs[i] >> 16) & 0xFF);
            *dst++ = (char)((brand_regs[i] >> 24) & 0xFF);
        }
        *dst = '\0';

        // Remove espaços à esquerda (a brand string do BIOS pode ter espaços)
        const char* trimmed = s_cpu_brand;
        while (*trimmed == ' ') trimmed++;
        // Move para o início do buffer se houver espaços
        if (trimmed != s_cpu_brand) {
            const char* src = trimmed;
            char* d = s_cpu_brand;
            while ((*d++ = *src++));
        }
    } else {
        // CPU muito antiga — usa o vendor como fallback
        kstrcpy(s_cpu_brand, s_cpu_vendor);
    }
}

// ============================================================
//  HardwareInfo::init
// ============================================================
void HardwareInfo::init() {
    if (s_initialized) return;
    detect_cpu();
    s_initialized = true;
}

// ============================================================
//  HardwareInfo — Getters públicos
// ============================================================
uint64_t HardwareInfo::get_total_ram() {
    return mem_get_total_bytes();
}

uint64_t HardwareInfo::get_free_ram() {
    return mem_get_free_pages() * 4096ULL;
}

uint64_t HardwareInfo::get_heap_used() {
    return mem_get_heap_used();
}

void HardwareInfo::format_ram(uint64_t bytes, char* buf) {
    if (bytes >= (1ULL << 30)) {
        // GB
        uint64_t gb = bytes >> 30;
        uint64_t rem = (bytes - (gb << 30)) >> 20;  // MB restantes
        char gb_str[8], rem_str[8];
        kuitoa(gb, gb_str);
        kuitoa(rem / 100, rem_str);  // 1 casa decimal
        kstrcpy(buf, gb_str);
        kstrcat(buf, ".");
        kstrcat(buf, rem_str);
        kstrcat(buf, " GB");
    } else {
        uint64_t mb = bytes >> 20;
        char mb_str[8];
        kuitoa(mb, mb_str);
        kstrcpy(buf, mb_str);
        kstrcat(buf, " MB");
    }
}

const char* HardwareInfo::get_cpu_name()   { return s_cpu_brand; }
const char* HardwareInfo::get_cpu_vendor()  { return s_cpu_vendor; }
uint32_t    HardwareInfo::get_cpu_cores()   { return s_cpu_cores; }

// ============================================================
//  API C (wrappers para o código C existente)
// ============================================================
extern "C" {

void sysinfo_init() {
    HardwareInfo::init();
}

uint64_t sysinfo_total_ram() {
    return HardwareInfo::get_total_ram();
}

uint64_t sysinfo_free_ram() {
    return HardwareInfo::get_free_ram();
}

const char* sysinfo_cpu_name() {
    return HardwareInfo::get_cpu_name();
}

const char* sysinfo_cpu_vendor() {
    return HardwareInfo::get_cpu_vendor();
}

uint32_t sysinfo_cpu_cores() {
    return HardwareInfo::get_cpu_cores();
}

void sysinfo_format_ram(uint64_t bytes, char* buf) {
    HardwareInfo::format_ram(bytes, buf);
}

} // extern "C"
