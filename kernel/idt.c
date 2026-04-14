// kernel/idt.c — IDT para 64-bit
#include "idt.h"
#include "types.h"

extern void irq0_handler(void);
extern void irq1_handler(void);
extern void irq12_handler(void);
extern void default_handler(void);

struct idt_entry {
    uint16_t off_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idt_ptr;

static void idt_set(int n, void (*handler)(void)) {
    uint64_t addr = (uint64_t)handler;
    idt[n].off_lo   = addr & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].ist      = 0;
    idt[n].flags    = 0x8E;
    idt[n].off_mid  = (addr >> 16) & 0xFFFF;
    idt[n].off_hi   = (addr >> 32) & 0xFFFFFFFF;
    idt[n].zero     = 0;
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set(i, default_handler);

    idt_set(32, irq0_handler);   // IRQ0 = timer
    idt_set(33, irq1_handler);   // IRQ1 = teclado
    idt_set(44, irq12_handler);  // IRQ12 = mouse (slave PIC → INT 40+4=44)

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
}
