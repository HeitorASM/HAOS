#include "clipboard.h"
#include "memory.h"

static char     g_clip[CLIPBOARD_SYS_MAX];
static uint32_t g_clip_len;

// O Editor (task da GUI) e as syscalls (outras tasks) tocam aqui: a cópia é
// curta, então basta desligar interrupções durante ela.
static inline uint64_t irq_save(void) {
    uint64_t f;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint64_t f) {
    if (f & (1ULL << 9)) __asm__ volatile ("sti" ::: "memory");
}

void clipboard_set(const char* data, uint32_t len) {
    if (!data) len = 0;
    if (len > CLIPBOARD_SYS_MAX) len = CLIPBOARD_SYS_MAX;
    uint64_t f = irq_save();
    if (len) kmemcpy(g_clip, data, len);
    g_clip_len = len;
    irq_restore(f);
}

uint32_t clipboard_get(char* out, uint32_t cap) {
    uint64_t f = irq_save();
    uint32_t total = g_clip_len;
    uint32_t n = total < cap ? total : cap;
    if (out && n) kmemcpy(out, g_clip, n);
    irq_restore(f);
    return total;
}

uint32_t clipboard_len(void) { return g_clip_len; }
