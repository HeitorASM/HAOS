// gui/gui.c — máquina de estados: BOOT → WELCOME → DESKTOP
#include "gui.h"
#include "window.h"
#include "terminal.h"
#include "../drivers/fb.h"
#include "../drivers/font.h"
#include "../drivers/mouse.h"
#include "../kernel/types.h"
#include "../kernel/memory.h"
#include "../kernel/keyboard.h"

extern volatile uint64_t timer_ticks;

static GuiState state = GUI_STATE_BOOT;

// ============================================================
//  Helpers de desenho
// ============================================================

// Desenha texto grande pixel-a-pixel (letras do logo HAOS no boot)
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

static void wait_ticks(uint64_t n) {
    uint64_t start = timer_ticks;
    while (timer_ticks - start < n) {
        keyboard_poll();
        mouse_process();
        __asm__("pause");
    }
}

// ============================================================
//  TELA DE BOOT — estilo minimal "loading ring"
// ============================================================

#define PROGRESS_STEPS 20

static void draw_boot_screen(int progress) {
    uint32_t sw = fb_width(), sh = fb_height();
    fb_clear(COLOR_BOOT_BG);

    // Fundo: gradiente vertical sutil
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

    // Barra de progresso estilada
    uint32_t bw = 320, bh = 6;
    uint32_t bx = (sw - bw) / 2, by = sh - 80;

    // Trilho
    fb_draw_rounded_rect(bx - 1, by - 1, bw + 2, bh + 2, 0x1A2840, 3);
    fb_fill_rect(bx, by, bw, bh, 0x080E18);

    // Preenchimento com gradiente
    int filled = (progress * (int)bw) / PROGRESS_STEPS;
    if (filled > 0)
        fb_fill_gradient_h(bx, by, (uint32_t)filled, bh,
                           COLOR_ACCENT_DARK, COLOR_ACCENT);

    // Glow na ponta da barra
    if (filled > 4)
        fb_fill_gradient_h((uint32_t)(bx + filled - 4), by, 8, bh,
                           COLOR_ACCENT, 0xAAD4FF);

    // Texto de status
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

static void run_boot_screen(void) {
    for (int i = 0; i <= PROGRESS_STEPS; i++) {
        draw_boot_screen(i);
        wait_ticks(8);
    }
    wait_ticks(40);
}

// ============================================================
//  TELA DE BOAS-VINDAS
// ============================================================

static void draw_welcome_screen(void) {
    uint32_t sw = fb_width(), sh = fb_height();

    // Fundo gradiente
    fb_fill_gradient_v(0, 0, sw, sh, 0x060810, 0x0E1428);

    // Reflexo sutil no topo
    fb_fill_gradient_v(0, 0, sw, 80, 0x0A1830, 0x060810);

    // Painel central
    uint32_t pw = 420, ph = 310;
    uint32_t px = (sw - pw) / 2, py = (sh - ph) / 2;

    // Sombra do painel
    fb_draw_shadow(px + 8, py + 8, pw, ph);

    // Painel principal
    fb_draw_rounded_rect(px, py, pw, ph, 0x0D1525, 10);
    fb_fill_rect(px + 2, py + 2, pw - 4, 1, 0x2244AA);  // brilho no topo

    // Header do painel (gradiente)
    fb_fill_gradient_v(px + 1, py + 1, pw - 2, 60, 0x0D2040, 0x0A1428);

    // Logo miniatura no header
    uint32_t mini_x = sw/2 - 60;
    uint32_t mini_y = py + 12;
    // Letras pequenas (1/2 do tamanho)
    fb_fill_rect(mini_x,      mini_y, 5, 28, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y+11, 14, 6, COLOR_ACCENT);
    fb_fill_rect(mini_x+9,    mini_y, 5, 28, COLOR_ACCENT);
    mini_x += 20;
    fb_fill_rect(mini_x+5,    mini_y, 5, 28, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y+11, 15, 6, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y, 6, 17, COLOR_ACCENT);
    fb_fill_rect(mini_x+9,    mini_y, 6, 17, COLOR_ACCENT);
    mini_x += 20;
    fb_draw_rounded_rect(mini_x, mini_y, 16, 28, COLOR_ACCENT, 5);
    fb_draw_rounded_rect(mini_x+3, mini_y+3, 10, 22, 0x0A1428, 4);
    mini_x += 22;
    fb_fill_rect(mini_x,      mini_y,    15, 5, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y+5,  6, 6, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y+11, 15, 6, COLOR_ACCENT);
    fb_fill_rect(mini_x+9,    mini_y+17, 6, 6, COLOR_ACCENT);
    fb_fill_rect(mini_x,      mini_y+22, 15, 6, COLOR_ACCENT);

    // Linha separadora
    fb_fill_gradient_h(px + 16, py + 62, pw - 32, 1, 0x0A1428, COLOR_ACCENT);
    fb_fill_gradient_h(px + 16, py + 63, pw - 32, 1, COLOR_ACCENT, 0x0A1428);

    // Texto central
    fb_draw_string_centered(px, py + 74, pw, 22,
                             "Bem-vindo ao HAOS", COLOR_TEXT_LIGHT, 0, true);
    fb_draw_string_centered(px, py + 100, pw, 16,
                             "Home-built Operating System v1.1",
                             COLOR_TEXT_GRAY, 0, true);
    fb_draw_string_centered(px, py + 120, pw, 14,
                             "x86-64  |  PS/2 Keyboard + Mouse  |  VESA 32bpp",
                             COLOR_TEXT_DIM, 0, true);

    // Separador
    fb_fill_rect(px + 40, py + 145, pw - 80, 1, 0x1A2840);

    // Recursos
    fb_draw_string_centered(px, py + 158, pw, 14,
                             "Double-buffered GUI  |  Janelas arrastáveis",
                             0x3A6A99, 0, true);
    fb_draw_string_centered(px, py + 174, pw, 14,
                             "Terminal integrado  |  Cursor do mouse",
                             0x3A6A99, 0, true);

    // Botão Entrar
    uint32_t btnw = 160, btnh = 38;
    uint32_t btnx = (sw - btnw) / 2, btny = py + ph - 66;
    fb_draw_rounded_rect(btnx, btny, btnw, btnh, COLOR_ACCENT_DARK, 8);
    fb_fill_gradient_v(btnx + 2, btny + 2, btnw - 4, btnh - 4,
                       0x3A7ACC, 0x1A4A90);
    fb_fill_rect(btnx + 4, btny + 2, btnw - 8, 1, 0x6AACFF);
    fb_draw_string_centered(btnx, btny, btnw, btnh,
                             "Entrar  [ENTER]", COLOR_TEXT_LIGHT, 0, true);

    // Dica
    fb_draw_string_centered(0, sh - 28, sw, 18,
                             "Pressione ENTER para continuar (ou aguarde 5s)",
                             COLOR_TEXT_DIM, 0, true);

    fb_flip();
}

// ============================================================
//  ÍCONES DO DESKTOP
// ============================================================

#define ICON_W 56
#define ICON_H 48
#define ICON_LABEL_H 14

// Ícone Terminal: fundo escuro + texto "> _"
static void draw_icon_terminal(uint32_t ix, uint32_t iy) {
    // Corpo do ícone
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x0D1925, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x2A4A6A);  // brilho

    // Barra de título miniatura
    fb_fill_gradient_h(ix + 2, iy + 2, ICON_W - 4, 10, 0x1A3A60, 0x0D1925);
    // Bolinhas de botão (red/yellow/green)
    fb_fill_circle(ix + 8,  iy + 7, 3, 0xE05555);
    fb_fill_circle(ix + 16, iy + 7, 3, 0xE0B840);
    fb_fill_circle(ix + 24, iy + 7, 3, 0x50B050);

    // Área de texto do terminal
    fb_fill_rect(ix + 2, iy + 13, ICON_W - 4, ICON_H - 15, COLOR_TERM_BG);
    fb_draw_string(ix + 5, iy + 16, ">", COLOR_TERM_PROMPT, 0, true);
    fb_draw_string(ix + 13, iy + 16, "_", COLOR_TERM_FG, 0, true);
    fb_draw_string(ix + 5, iy + 26, "$ haos", 0x446688, 0, true);
    fb_draw_string(ix + 5, iy + 36, "~$", 0x336655, 0, true);

    // Label
    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Terminal",
                             COLOR_TEXT_LIGHT, 0, true);
}

// Ícone Sobre: janela info
static void draw_icon_about(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x0E1830, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x2A4A7A);
    fb_fill_gradient_v(ix + 2, iy + 2, ICON_W - 4, ICON_H - 4, 0x0E1830, 0x0A1020);

    // Letra "i" estilizada
    fb_fill_circle(ix + ICON_W/2, iy + 14, 5, COLOR_ACCENT);
    fb_fill_circle(ix + ICON_W/2, iy + 14, 3, 0x0E1830);
    fb_fill_circle(ix + ICON_W/2, iy + 14, 2, COLOR_ACCENT);  // ponto do i
    fb_fill_rect((uint32_t)(ix + ICON_W/2 - 3), iy + 22, 6, 16, COLOR_ACCENT);
    fb_fill_rect((uint32_t)(ix + ICON_W/2 - 5), iy + 35, 10, 4, COLOR_ACCENT);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Sobre",
                             COLOR_TEXT_LIGHT, 0, true);
}

// Ícone Configurações (engrenagem simplificada)
static void draw_icon_settings(uint32_t ix, uint32_t iy) {
    fb_draw_rounded_rect(ix, iy, ICON_W, ICON_H, 0x111520, 6);
    fb_fill_rect(ix + 2, iy + 2, ICON_W - 4, 1, 0x303848);

    uint32_t cx = ix + ICON_W/2, cy = iy + ICON_H/2;
    // Engrenagem: círculo externo + interno
    fb_fill_circle(cx, cy, 16, 0x3A5A7A);
    fb_fill_circle(cx, cy, 12, 0x111520);
    fb_fill_circle(cx, cy,  6, 0x3A5A7A);
    fb_fill_circle(cx, cy,  3, 0x111520);
    // Dentes
    for (int i = 0; i < 8; i++) {
        // Simulação de dentes como retângulos em 4 direções
    }
    fb_fill_rect(cx - 2, iy + 4, 4, 6, 0x3A5A7A);
    fb_fill_rect(cx - 2, iy + ICON_H - 10, 4, 6, 0x3A5A7A);
    fb_fill_rect(ix + 4, cy - 2, 6, 4, 0x3A5A7A);
    fb_fill_rect(ix + ICON_W - 10, cy - 2, 6, 4, 0x3A5A7A);

    fb_draw_string_centered(ix - 4, iy + ICON_H + 4, ICON_W + 8,
                             ICON_LABEL_H, "Config.",
                             COLOR_TEXT_LIGHT, 0, true);
}

static void draw_desktop_icons(void) {
    draw_icon_terminal(20, 20);
    draw_icon_about(20, 20 + ICON_H + ICON_LABEL_H + 16);
    draw_icon_settings(20, 20 + (ICON_H + ICON_LABEL_H + 16) * 2);
}

// ============================================================
//  TASKBAR
// ============================================================

#define TASKBAR_H    40
#define START_BTN_W  100
#define CLOCK_W      72

static bool     start_menu_open  = false;
static Window*  terminal_win     = NULL;
static uint64_t last_render_tick = 0;
static uint64_t bg_dirty         = 1;  // força redraw do fundo no 1º frame

static void draw_taskbar(uint64_t ticks) {
    uint32_t sw = fb_width(), sh = fb_height();
    uint32_t ty = sh - TASKBAR_H;

    // Fundo da taskbar: gradiente + blur visual
    fb_fill_gradient_v(0, ty, sw, TASKBAR_H, 0x0A0E1A, 0x060810);
    fb_fill_rect(0, ty, sw, 1, COLOR_TASKBAR_LINE);  // linha separadora

    // Reflexo sutil
    fb_fill_gradient_h(0, ty + 1, sw, 1, 0x1A2840, 0x0A0E1A);

    // Botão Iniciar
    uint32_t sb_col = start_menu_open ? COLOR_ACCENT_DARK : 0x0E1A30;
    fb_draw_rounded_rect(4, ty + 5, START_BTN_W, TASKBAR_H - 10, sb_col, 5);
    if (start_menu_open)
        fb_fill_rect(4, ty + 5, START_BTN_W, 1, 0x4A80CC);
    fb_draw_string_centered(4, ty + 5, START_BTN_W, TASKBAR_H - 10,
                             "# Iniciar", COLOR_TEXT_LIGHT, 0, true);

    // Botão do terminal na taskbar
    int ax = START_BTN_W + 12;
    if (terminal_win && terminal_win->active) {
        bool focused = (wm_get_focused() == terminal_win);
        uint32_t tbtn = focused ? 0x1A3A6A : 0x0C1220;
        fb_draw_rounded_rect((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10, tbtn, 4);
        if (focused)
            fb_fill_rect((uint32_t)ax, ty + 5, 130, 1, 0x3A7ACC);
        fb_draw_string_centered((uint32_t)ax, ty + 5, 130, TASKBAR_H - 10,
                                 "Terminal", COLOR_TEXT_LIGHT, 0, true);
    }

    // Relógio digital
    char tbuf[12];
    uint64_t secs  = ticks / 100;
    uint64_t mins  = secs  / 60;
    uint64_t hours = mins  / 60;
    secs  %= 60; mins %= 60; hours %= 24;
    tbuf[0] = '0' + (char)(hours/10); tbuf[1] = '0' + (char)(hours%10);
    tbuf[2] = ':';
    tbuf[3] = '0' + (char)(mins/10);  tbuf[4] = '0' + (char)(mins%10);
    tbuf[5] = ':';
    tbuf[6] = '0' + (char)(secs/10);  tbuf[7] = '0' + (char)(secs%10);
    tbuf[8] = 0;

    uint32_t clock_x = sw - CLOCK_W - 8;
    fb_draw_rounded_rect(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                         0x0C1220, 4);
    fb_draw_string_centered(clock_x - 4, ty + 5, CLOCK_W + 8, TASKBAR_H - 10,
                             tbuf, COLOR_ACCENT, 0, true);
}

// ============================================================
//  MENU INICIAR
// ============================================================

static void draw_start_menu(void) {
    uint32_t sh = fb_height();
    uint32_t mw = 200, mh = 240;
    uint32_t mx = 4, my = sh - TASKBAR_H - mh;

    // Sombra
    fb_draw_shadow(mx + 6, my + 6, mw, mh);

    // Fundo
    fb_draw_rounded_rect(mx, my, mw, mh, 0x0A0F1E, 8);
    fb_fill_rect(mx + 1, my + 1, mw - 2, 1, 0x2A4A8A);

    // Header com gradiente
    fb_fill_gradient_v(mx + 1, my + 1, mw - 2, 44, 0x0D2040, 0x08101A);
    fb_fill_rect(mx + 1, my + 44, mw - 2, 1, 0x0A1428);
    fb_draw_string_centered(mx, my + 1, mw, 44,
                             "HAOS  v1.1", COLOR_TEXT_LIGHT, 0, true);

    // Itens
    const char* items[] = {
        "  [T]  Terminal",
        "  [A]  Sobre o HAOS",
        "",
        "  [R]  Reiniciar",
        "  [Q]  Desligar",
    };
    int item_h = 34;
    int iy = (int)my + 50;
    for (int i = 0; i < 5; i++) {
        if (!items[i][0]) {
            fb_fill_rect(mx + 12, (uint32_t)(iy + item_h/2), mw - 24, 1, 0x1A2840);
        } else {
            // Hover: fundo sutil para cada item
            fb_fill_rect(mx + 4, (uint32_t)iy, mw - 8, item_h - 2, 0x0C1428);
            fb_draw_string(mx + 10, (uint32_t)(iy + (item_h - FONT_H)/2),
                           items[i], COLOR_TEXT_LIGHT, 0, true);
        }
        iy += item_h;
    }

    fb_draw_string_centered(mx, (uint32_t)(my + mh - 22), mw, 18,
                             "[ESC] fechar",
                             COLOR_TEXT_DIM, 0, true);
}

// ============================================================
//  JANELA SOBRE
// ============================================================

static void about_draw(Window* win) {
    int bx = win->x + WIN_BORDER + 16;
    int by = win->y + WIN_BORDER + TITLE_BAR_H + 16;

    fb_draw_string((uint32_t)bx, (uint32_t)by,
                   "HAOS — Home-built OS", COLOR_ACCENT, 0, true);
    by += 6;
    fb_fill_rect((uint32_t)bx, (uint32_t)by, 180, 1, 0x1A3A6A);
    by += 12;

    const char* lines[] = {
        "Versao    : 1.1",
        "Arq.      : x86-64 Long Mode",
        "Boot      : GRUB2 + Multiboot2",
        "Video     : VESA VBE 1024x768 32bpp",
        "GUI       : double-buffered FB",
        "Input     : PS/2 Keyboard + Mouse",
        "Kernel    : C freestanding (sem libc)",
        "",
        "Desenvolvido do zero em C + ASM",
    };
    for (int i = 0; i < 9; i++) {
        uint32_t col = (lines[i][0] == 0) ? 0 : (i == 8 ? COLOR_TEXT_GRAY : COLOR_TEXT_LIGHT);
        if (lines[i][0])
            fb_draw_string((uint32_t)bx, (uint32_t)by, lines[i], col, 0, true);
        by += 20;
    }
}

static void open_about_window(void) {
    uint32_t sw = fb_width(), sh = fb_height();
    Window* w = wm_create(WIN_TYPE_ABOUT,
                          (int32_t)(sw/2 - 220), (int32_t)(sh/2 - 150),
                          440, 300, "Sobre o HAOS");
    if (w) { w->draw_content = about_draw; w->on_key = NULL; }
}

// ============================================================
//  LOOP DO DESKTOP
// ============================================================

// Teclado: a tecla só é tratada como hotkey se NENHUMA janela está focada
// ou se é ESC/Menu. Caso contrário, vai para a janela focada.
static void handle_desktop_key(uint8_t c) {
    // ESC sempre fecha o menu
    if (c == 27) {
        start_menu_open = false;
        return;
    }

    // Se o menu está aberto, teclas navegam nele
    if (start_menu_open) {
        if (c == 't' || c == 'T') {
            if (!terminal_win || !terminal_win->active) {
                uint32_t sw = fb_width(), sh = fb_height();
                terminal_win = terminal_create(
                    (int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
            } else { wm_focus(terminal_win); }
            start_menu_open = false;
        } else if (c == 'a' || c == 'A') {
            open_about_window();
            start_menu_open = false;
        } else if (c == 'r' || c == 'R') {
            outb(0x64, 0xFE);
            while(1) __asm__("hlt");
        }
        return;
    }

    // F1 abre/fecha menu (não conflita com nada digitável no terminal)
    // Como só temos ASCII básico, usamos Ctrl+S = 0x13 p/ menu
    // (mas para não quebrar o terminal, a melhor solução é:
    //  se há janela focada com on_key → despacha para ela SEMPRE)
    Window* focused = wm_get_focused();
    if (focused && focused->on_key) {
        // Despacha para a janela (terminal recebe o char)
        wm_dispatch_key(c);
        return;
    }

    // Sem janela focada: hotkeys globais
    if (c == 's' || c == 'S') { start_menu_open = !start_menu_open; return; }
    if (c == 't' || c == 'T') {
        if (!terminal_win || !terminal_win->active) {
            uint32_t sw = fb_width(), sh = fb_height();
            terminal_win = terminal_create(
                (int32_t)(sw/2 - 340), (int32_t)(sh/2 - 200));
        } else { wm_focus(terminal_win); }
        return;
    }
    if (c == 'a' || c == 'A') { open_about_window(); return; }
}

static void run_desktop(void) {
    uint32_t sw = fb_width(), sh = fb_height();

    // Configura limites do mouse
    mouse_set_bounds((int32_t)sw - 1, (int32_t)sh - 1);

    // Abre terminal automaticamente
    terminal_win = terminal_create((int32_t)(sw/2 - 340),
                                   (int32_t)(sh/2 - 200));
    bg_dirty = 1;

    // Estado anterior do mouse para detecção de drag
    bool was_pressed = false;

    while (1) {
        uint64_t ticks = timer_ticks;

        // --- Processa entradas ---
        uint8_t c;
        while ((c = keyboard_getchar()) != 0)
            handle_desktop_key(c);

        // --- Mouse ---
        mouse_process();
        mouse_snap();

        int32_t mx = mouse_get_x(), my = mouse_get_y();
        bool pressed = mouse_left_pressed();

        if (pressed && !was_pressed) {
            // Clique na taskbar
            uint32_t ty = sh - TASKBAR_H;
            if (my >= (int32_t)ty) {
                // Botão Iniciar
                if (mx >= 4 && mx < 4 + START_BTN_W)
                    start_menu_open = !start_menu_open;
                // Botão terminal na taskbar
                else if (terminal_win && terminal_win->active &&
                         mx >= START_BTN_W + 12 && mx < START_BTN_W + 12 + 130) {
                    wm_focus(terminal_win);
                    start_menu_open = false;
                }
            }
            // Menu iniciar aberto: clique em itens
            else if (start_menu_open) {
                uint32_t mh = 240;
                uint32_t menu_x = 4, menu_y = sh - TASKBAR_H - mh;
                if (mx >= (int32_t)menu_x && mx < (int32_t)(menu_x + 200)) {
                    int rel_y = my - (int32_t)menu_y - 50;
                    int item = rel_y / 34;
                    if (item == 0) {  // Terminal
                        if (!terminal_win || !terminal_win->active)
                            terminal_win = terminal_create(
                                (int32_t)(sw/2-340), (int32_t)(sh/2-200));
                        else wm_focus(terminal_win);
                        start_menu_open = false;
                    } else if (item == 1) {  // Sobre
                        open_about_window();
                        start_menu_open = false;
                    } else if (item == 3) {  // Reiniciar
                        outb(0x64, 0xFE);
                        while(1) __asm__("hlt");
                    }
                } else {
                    start_menu_open = false;
                }
            }
            // Janelas
            else {
                wm_mouse_down(mx, my);
            }
        }

        if (pressed && was_pressed) {
            wm_mouse_move(mx, my);
        }

        if (!pressed && was_pressed) {
            wm_mouse_up();
        }

        was_pressed = pressed;

        // --- Renderiza apenas a cada 2 ticks (~20ms @ 100Hz = ~50fps) ---
        if (ticks - last_render_tick < 2) {
            __asm__("pause");
            continue;
        }
        last_render_tick = ticks;

        // Atualiza cursor piscante do terminal
        if (terminal_win) terminal_tick(terminal_win, ticks);

        // ---- Fundo do desktop ----
        fb_fill_gradient_v(0, 0, sw, sh - TASKBAR_H, COLOR_BG, 0x0B1020);

        // Padrão de pontos sutil no fundo
        for (uint32_t gy = 0; gy < (sh - TASKBAR_H); gy += 32)
            for (uint32_t gx = 0; gx < sw; gx += 32)
                fb_put_pixel(gx, gy, 0x0F1828);

        // Texto de marca d'água no centro
        fb_draw_string_centered(0, sh/2 - 60, sw, 20,
                                 "HAOS Desktop", 0x111E30, 0, true);

        // Ícones
        draw_desktop_icons();

        // Janelas
        wm_draw_all();

        // Taskbar (por cima das janelas)
        draw_taskbar(ticks);

        // Menu Iniciar
        if (start_menu_open) draw_start_menu();

        // Dica de atalhos (só se o menu estiver fechado)
        if (!start_menu_open) {
            fb_draw_string(START_BTN_W + 14, sh - TASKBAR_H + 13,
                           "[S] Menu  [T] Terminal  [A] Sobre",
                           COLOR_TEXT_DIM, 0, true);
        }

        // Cursor do mouse (sempre por cima de tudo)
        fb_draw_cursor((uint32_t)mx, (uint32_t)my);

        // Flip: envia shadow buffer → framebuffer real (sem jitter)
        fb_flip();
    }
}

// ============================================================
//  API PÚBLICA
// ============================================================

void gui_init(void) {
    wm_init();
    state = GUI_STATE_BOOT;
}

void gui_run(void) {
    run_boot_screen();
    state = GUI_STATE_WELCOME;
    draw_welcome_screen();

    // Aguarda ENTER, espaço ou timeout de 5s
    uint64_t start = timer_ticks;
    while (1) {
        mouse_process();
        uint8_t c = keyboard_getchar();
        if (c == '\n' || c == '\r' || c == ' ') break;
        if (timer_ticks - start > 500) break;
        __asm__("pause");
    }

    state = GUI_STATE_DESKTOP;
    run_desktop();
}
