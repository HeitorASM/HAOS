#pragma once
#include "types.h"

// ============================================================
//  sysinfo.h — Deteção de hardware do HAOS
//
//  Fornece informações sobre CPU e RAM para a UI de configurações.
//  Toda a deteção é feita via CPUID (para CPU) e via o gestor
//  de memória (para RAM). Sem dependências externas.
// ============================================================

#ifdef __cplusplus

class HardwareInfo {
public:
    // ---- Inicialização (chama uma única vez após memory_init) ----
    static void init(void);

    // ---- RAM ----
    // Retorna a RAM total instalada em bytes (detetada via Multiboot2 mmap)
    static uint64_t get_total_ram(void);

    // Retorna a RAM livre aproximada em bytes (páginas livres × 4096)
    static uint64_t get_free_ram(void);

    // Retorna a RAM usada pela heap do kernel em bytes
    static uint64_t get_heap_used(void);

    // Formata RAM para exibição: "512 MB" ou "2 GB"
    // buf deve ter pelo menos 16 bytes
    static void format_ram(uint64_t bytes, char* buf);

    // ---- CPU ----
    // Retorna a brand string do processador (ex: "Intel(R) Core(TM) i7-9700K")
    // O buffer interno tem 49 bytes. Retorna ponteiro para buffer estático.
    static const char* get_cpu_name(void);

    // Retorna apenas o fabricante (ex: "GenuineIntel" ou "AuthenticAMD")
    static const char* get_cpu_vendor(void);

    // Retorna o número de núcleos lógicos (via CPUID leaf 0xB ou 1)
    static uint32_t get_cpu_cores(void);

private:
    // ---- Dados estáticos (sem heap — init() preenche uma vez) ----
    static char     s_cpu_brand[49];   // 3 × 16 bytes + '\0'
    static char     s_cpu_vendor[13];  // "GenuineIntel\0"
    static uint32_t s_cpu_cores;
    static bool     s_initialized;

    // ---- Helpers ----
    static void detect_cpu(void);
};

extern "C" {
#endif

// ---- API C para o código C existente (terminal, about, config) ----
void        sysinfo_init(void);
uint64_t    sysinfo_total_ram(void);     // bytes
uint64_t    sysinfo_free_ram(void);      // bytes
const char* sysinfo_cpu_name(void);
const char* sysinfo_cpu_vendor(void);
uint32_t    sysinfo_cpu_cores(void);
// Formata bytes em "X MB" ou "X GB" (buf ≥ 16 bytes)
void        sysinfo_format_ram(uint64_t bytes, char* buf);

#ifdef __cplusplus
} // extern "C"
#endif
