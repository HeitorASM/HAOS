#include "pit.h"
#include "types.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_FREQ 1193182  // frequência base do PIT em Hz

void pit_init(uint32_t hz) {
    uint32_t divisor = PIT_FREQ / hz;
    // Canal 0, modo 3 (square wave), acesso lo/hi
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}
