// kernel/gdt.c — Global Descriptor Table (64-bit)
#include "gdt.h"
#include "types.h"

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_hi;
    uint8_t  base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr   gdt_ptr;

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags) {
    gdt[i].limit_lo       = limit & 0xFFFF;
    gdt[i].base_lo        = base  & 0xFFFF;
    gdt[i].base_mid       = (base  >> 16) & 0xFF;
    gdt[i].access         = access;
    gdt[i].flags_limit_hi = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_hi        = (base  >> 24) & 0xFF;
}

// Definido em gdt_asm.asm
extern void gdt_flush(uint64_t ptr);

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    gdt_set(0, 0, 0x00000, 0x00, 0x0); // null
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0x2); // kernel code: P=1 DPL=0 E=1 L=1
    gdt_set(2, 0, 0xFFFFF, 0x92, 0x0); // kernel data: P=1 DPL=0 W=1

    gdt_flush((uint64_t)&gdt_ptr);
}
