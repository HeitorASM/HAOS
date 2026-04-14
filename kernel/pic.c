// kernel/pic.c — 8259A PIC: remapeia IRQs 0–15 para INT 0x20–0x2F
#include "pic.h"
#include "types.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20
#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_init(void) {
    // ICW1
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);  io_wait();

    // ICW2: vetores base
    outb(PIC1_DATA, 0x20);  io_wait();   // IRQ0-7  → INT 32-39
    outb(PIC2_DATA, 0x28);  io_wait();   // IRQ8-15 → INT 40-47

    // ICW3: cascata
    outb(PIC1_DATA, 0x04);  io_wait();   // slave em IRQ2
    outb(PIC2_DATA, 0x02);  io_wait();   // id cascade = 2

    // ICW4: modo 8086
    outb(PIC1_DATA, ICW4_8086);  io_wait();
    outb(PIC2_DATA, ICW4_8086);  io_wait();

    // Máscaras:
    // Master: habilita IRQ0 (timer) + IRQ1 (teclado) + IRQ2 (cascade p/ slave)
    //   0xF8 = 1111 1000 → bits 0,1,2 limpos = IRQ0,1,2 habilitados
    outb(PIC1_DATA, 0xF8);

    // Slave: habilita IRQ12 (mouse) — bit 4 do slave (IRQ12-8=4)
    //   0xEF = 1110 1111 → bit 4 limpo = IRQ12 habilitado
    outb(PIC2_DATA, 0xEF);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) | (1 << irq));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, inb(port) & ~(1 << irq));
}
