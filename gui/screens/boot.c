#include "boot.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/keyboard.h"   
#include "../../drivers/mouse.h"
#include "../../kernel/types.h"

extern volatile uint64_t timer_ticks;

#define PROGRESS_STEPS 20

static void wait_ticks(uint64_t n) {
    uint64_t start = timer_ticks;
    while (timer_ticks - start < n) {
        keyboard_poll();
        mouse_process();
        __asm__("pause");
    }
}

// Desenho das letras do logo HAOS (pixel art)
static void draw_logo_letter_H(uint32_t lx, uint32_t ly, uint32_t col) {
    fb_fill_rect(lx,      ly, 12, 70, col);
    fb_fill_rect(lx,      ly+28, 36, 14, col);
    fb_fill_rect(lx+24,   ly, 12, 70, col);
}
static void draw_logo_letter_A(uint32_t lx, uint32_t ly, uint32_t col) {
    fb_fill_rect(lx+12,   ly, 12, 70, col);
    fb_fill_rect(lx,      ly+28, 36, 14, col);
    fb_fill_rect(lx,      ly, 14, 42, col);
    fb_fill_rect(lx+22,   ly, 14, 42, col);
}
static void draw_logo_letter_O(uint32_t lx, uint32_t ly, uint32_t col) {
    fb_draw_rounded_rect(lx, ly, 36, 70, col, 10);
    fb_draw_rounded_rect(lx+4, ly+4, 28, 62, COLOR_BOOT_BG, 8);
}
static void draw_logo_letter_S(uint32_t lx, uint32_t ly, uint32_t col) {
    fb_fill_rect(lx,      ly,    36, 14, col);
    fb_fill_rect(lx,      ly+14, 14, 14, col);
    fb_fill_rect(lx,      ly+28, 36, 14, col);
    fb_fill_rect(lx+22,   ly+42, 14, 14, col);
    fb_fill_rect(lx,      ly+56, 36, 14, col);
}

static void draw_boot_screen(int progress) {
    uint32_t sw = fb_width(), sh = fb_height();
    fb_clear(COLOR_BOOT_BG);

    // Fundo com gradiente vertical
    fb_fill_gradient_v(0, 0, sw, sh, 0x060810, 0x0B0E1A);

    // Logo HAOS centrado
    uint32_t lx = sw/2 - 182, ly = sh/2 - 80;
    draw_logo_letter_H(lx,        ly, COLOR_ACCENT);
    draw_logo_letter_A(lx + 50,   ly, COLOR_ACCENT);
    draw_logo_letter_O(lx + 100,  ly, COLOR_ACCENT);
    draw_logo_letter_S(lx + 150,  ly, COLOR_ACCENT);

    // Brilho abaixo do logo
    fb_fill_gradient_v(sw/2 - 150, ly + 74, 300, 6,
                       COLOR_ACCENT, COLOR_BOOT_BG);

    // Subtítulo
    fb_draw_string_centered(0, sh/2 + 20, sw, 18,
                             "Home-built Operating System  v1.1",
                             0x4466AA, COLOR_BOOT_BG, false);

    // Barra de progresso estilizada
    uint32_t bw = 320, bh = 6;
    uint32_t bx = (sw - bw) / 2, by = sh - 80;

    fb_draw_rounded_rect(bx - 1, by - 1, bw + 2, bh + 2, 0x1A2840, 3);
    fb_fill_rect(bx, by, bw, bh, 0x080E18);

    int filled = (progress * (int)bw) / PROGRESS_STEPS;
    if (filled > 0)
        fb_fill_gradient_h(bx, by, (uint32_t)filled, bh,
                           COLOR_ACCENT_DARK, COLOR_ACCENT);
    if (filled > 4)
        fb_fill_gradient_h((uint32_t)(bx + filled - 4), by, 8, bh,
                           COLOR_ACCENT, 0xAAD4FF);

    // Mensagens de status
    const char* msgs[PROGRESS_STEPS] = {
        "Inicializando hardware...",
        "Configurando GDT...",
        "Carregando IDT...",
        "Inicializando PIC...",
        "Configurando timer 100Hz...",
        "Detectando memória...",
        "Alocando shadow buffer...",
        "Carregando drivers...",
        "Inicializando teclado PS/2...",
        "Inicializando mouse PS/2...",
        "Inicializando GUI...",
        "Carregando gerenciador de janelas...",
        "Preparando área de trabalho...",
        "Renderizando fundo...",
        "Aplicando paleta de cores...",
        "Compilando ícones...",
        "Iniciando serviços...",
        "Carregando terminal...",
        "Finalizando inicialização...",
        "Bem-vindo ao HAOS!",
    };

    int idx = progress < PROGRESS_STEPS ? progress : PROGRESS_STEPS - 1;
    fb_draw_string_centered(0, by + 14, sw, 16,
                             msgs[idx], COLOR_TEXT_GRAY, COLOR_BOOT_BG, false);

    // Percentual
    char pct[8];
    int p = (progress * 100) / PROGRESS_STEPS;
    pct[0] = '0' + (char)(p/100);
    pct[1] = '0' + (char)((p/10)%10);
    pct[2] = '0' + (char)(p%10);
    pct[3] = '%'; pct[4] = 0;
    fb_draw_string_centered((uint32_t)(bx + bw + 10), by - 1, 40, bh + 2,
                             pct, COLOR_TEXT_GRAY, 0, true);

    fb_flip();
}

void run_boot_screen(void) {
    for (int i = 0; i <= PROGRESS_STEPS; i++) {
        draw_boot_screen(i);
        wait_ticks(8);
    }
    wait_ticks(40);
}