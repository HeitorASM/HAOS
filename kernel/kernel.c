#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "memory.h"
#include "paging.h"
#include "syscall.h"
#include "usermode.h"
#include "task.h"
#include "scheduler.h"
#include "sysinfo.h"
#include "lang.h"
#include "../drivers/fb.h"
#include "../drivers/mouse.h"
#include "../drivers/rtc.h"
#include "../drivers/ata_pio.h"
#include "../drivers/storage.h"
#include "../gui/gui.h"
#include "../fs/vfs.h"

extern void set_current_syscall_stack(uint64_t top); // syscall_asm.asm

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
    // GDT (agora inclui seletores de usuário + TSS)
    gdt_init();

    // Valor inicial seguro para current_syscall_stack_top (ver
    // comentário grande em kernel/syscall_asm.asm) — ANTES de
    // qualquer processo de usuário poder existir. Na prática
    // syscall_entry só é alcançável a partir de Ring 3, então isso
    // seria redundante com o que scheduler.c/do_switch_to já
    // configura na primeira troca para um processo de usuário real —
    // mas não custa nada garantir que a variável nunca fica com o
    // valor 0 (BSS zerado) nessa janela, por defesa.
    set_current_syscall_stack(gdt_get_initial_kernel_stack_top());

    // IDT + interrupções
    idt_init();
    pic_init();
    pit_init(100);

    // Teclado e Mouse
    keyboard_init();

    // ---- Scheduler / multitarefa (etapa 1: kernel threads) ----
    // PRECISA vir ANTES do "sti" abaixo: assim que interrupções são
    // habilitadas, o PIT (IRQ0) começa a chamar scheduler_tick() a
    // cada 10ms. Sem a fila do scheduler já montada, isso seria uma
    // desreferência de ponteiro nulo dentro do handler de interrupção
    // — trava o boot inteiro antes até do framebuffer ser inicializado,
    // sem nenhuma mensagem na tela (foi exatamente esse o bug que
    // causou tela 100% preta ao testar no VirtualBox: o "sti" já
    // rodava bem antes de tasking_init()/scheduler_init() existirem).
    // scheduler_tick() também tem uma guarda defensiva própria para o
    // caso de outro ponto de inicialização futuro cometer o mesmo
    // deslize, mas a ordem correta é esta: inicializar o scheduler
    // antes de habilitar interrupções, não depois.
    tasking_init();
    scheduler_init();

    // Habilita interrupções
    __asm__ volatile("sti");

    // Verifica Multiboot2
    if (magic != MB2_MAGIC)
        kpanic("Nao iniciado via Multiboot2!");

    // Inicializa a memória PRIMEIRO
    memory_init(mb_info_raw);

    // Substitui o mapeamento temporário de 2MB do boot.asm por uma
    // hierarquia de tabelas de página real, com granularidade de
    // 4KB e um handler de #PF que resolve demand paging (kernel.c
    // continua identity-mapped por enquanto — higher-half fica para
    // quando implementarmos Ring 3 com espaços de endereço próprios
    // por processo).
    paging_init();

    // Percorre tags para encontrar o framebuffer
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

    // Inicializa framebuffer
    fb_init(fb_addr, fb_w, fb_h, fb_pitch);

    // Inicializa deteção de hardware
    sysinfo_init();

    // Aloca buffers de vídeo na heap real
    uint32_t fb_bytes = fb_h * fb_pitch;
    void* shadow  = kmalloc(fb_bytes);
    void* bgcache = kmalloc(fb_bytes);
    if (shadow)  fb_set_backbuffer(shadow);
    if (bgcache) fb_set_bg_cache(bgcache);

    mouse_init();
    rtc_init();
    storage_init();    // IDE/ATA + SATA/AHCI, com regras de segurança (drivers/storage.c)
    vfs_init();
    lang_init();

    // Habilita SYSCALL/SYSRET (MSRs STAR/LSTAR/SFMASK + EFER.SCE).
    // A infraestrutura de Ring 3 (paging_map com PAGE_USER,
    // enter_usermode, syscalls sys_write/sys_exit) já está pronta e
    // foi validada ponta a ponta com um processo de teste — removido
    // daqui após a validação. O primeiro uso real volta junto com o
    // carregador de executáveis + scheduler.
    syscall_init();

    gui_init();
    gui_run();

    while (1) __asm__("hlt");
}