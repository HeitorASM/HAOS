// kernel/kernel.c — entrada principal do kernel HAOS
#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "memory.h"
#include "../drivers/fb.h"
#include "../drivers/mouse.h"
#include "../gui/gui.h"

#define MB2_MAGIC  0x36D76289U
#define MB2_TAG_FB 8
#define MB2_TAG_END 0

typedef struct { uint32_t type; uint32_t size; } __attribute__((packed)) MB2Tag;

typedef struct {
    MB2Tag   tag;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint16_t reserved;
} __attribute__((packed)) MB2TagFB;

static void kpanic(const char* msg) {
    fb_init(0xFD000000, 1024, 768, 4096);
    fb_clear(0x3D0000);
    fb_draw_string(20, 20, "!!! KERNEL PANIC !!!", COLOR_WHITE, 0x3D0000, false);
    fb_draw_string(20, 50, msg, COLOR_WHITE, 0x3D0000, false);
    while (1) __asm__("hlt");
}

void kernel_main(uint32_t magic, uint32_t mb_info_raw) {
    // 1. GDT
    gdt_init();

    // 2. IDT + interrupções
    idt_init();
    pic_init();
    pit_init(100);   // 100 Hz

    // 3. Teclado e Mouse
    keyboard_init();

    // 4. Habilita interrupções
    __asm__ volatile("sti");

    // 5. Verifica Multiboot2
    if (magic != MB2_MAGIC)
        kpanic("Nao iniciado via Multiboot2!");

    // 6. Percorre tags para encontrar o framebuffer
    uint8_t* ptr = (uint8_t*)(uintptr_t)(mb_info_raw + 8);
    MB2Tag*  tag = (MB2Tag*)ptr;

    uint64_t fb_addr  = 0;
    uint32_t fb_pitch = 0, fb_w = 0, fb_h = 0;

    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_FB) {
            MB2TagFB* fb = (MB2TagFB*)tag;
            fb_addr  = fb->addr;
            fb_pitch = fb->pitch;
            fb_w     = fb->width;
            fb_h     = fb->height;
        }
        uint32_t sz = tag->size;
        if (sz & 7) sz = (sz + 7) & ~7U;
        if (sz < 8) break;
        tag = (MB2Tag*)((uint8_t*)tag + sz);
    }

    if (fb_addr == 0)
        kpanic("Framebuffer nao encontrado no Multiboot2!");

    // 7. Inicializa framebuffer
    fb_init(fb_addr, fb_w, fb_h, fb_pitch);

    // 8. Inicializa heap
    memory_init();

    // 9. Aloca shadow buffer (double buffering) + cache de fundo
    uint32_t fb_bytes = fb_h * fb_pitch;
    void* shadow = kmalloc(fb_bytes);
    void* bgcache = kmalloc(fb_bytes);
    if (shadow)  fb_set_backbuffer(shadow);
    if (bgcache) fb_set_bg_cache(bgcache);

    // 10. Inicializa mouse PS/2
    mouse_init();

    // 11. Inicia GUI (não retorna)
    gui_init();
    gui_run();

    while (1) __asm__("hlt");
}
