#pragma once
#include "types.h"

// ============================================================
//  multiboot2.h — Estruturas do protocolo Multiboot2
//  Ref: https://www.gnu.org/software/grub/manual/multiboot2/
// ============================================================

#define MB2_MAGIC_BOOTLOADER  0x36D76289U

// Tipos de tag
#define MB2_TAG_END           0
#define MB2_TAG_CMDLINE       1
#define MB2_TAG_BOOTLOADER    2
#define MB2_TAG_MMAP          6   // Memory Map — fundamental para o PFA
#define MB2_TAG_FRAMEBUFFER   8
#define MB2_TAG_ACPI_OLD      14
#define MB2_TAG_ACPI_NEW      15

// Tipos de entrada no mmap
#define MB2_MMAP_AVAILABLE    1   // RAM utilizável
#define MB2_MMAP_RESERVED     2
#define MB2_MMAP_ACPI_RECLAM  3
#define MB2_MMAP_NVS          4
#define MB2_MMAP_BAD          5

// ---- Cabeçalho geral do boot info ----
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) mb2_info_t;

// ---- Tag base ----
typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

// ---- Tag do framebuffer (tipo 8) ----
typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) mb2_tag_framebuffer_t;

// ---- Entrada individual do mapa de memória ----
typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;      // MB2_MMAP_*
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

// ---- Tag do mapa de memória (tipo 6) ----
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    // Seguido de mb2_mmap_entry_t[]
} __attribute__((packed)) mb2_tag_mmap_t;
