#pragma once
#include "types.h"

// ============================================================
//  libc/include/haos/gfx.h — janelas gráficas para programas Ring 3
//
//  Um programa cria uma janela, desenha num canvas que pertence ao
//  kernel e recebe teclado/mouse como eventos:
//
//      int win = win_create(400, 300, "Meu app");
//      win_fill_rect(win, 0, 0, 400, 300, 0x101830);
//      win_text(win, 10, 10, "Ola!", 0xFFFFFF);
//
//      struct haos_win_event ev;
//      for (;;) {
//          int r = win_event(win, &ev, WIN_WAIT_FOREVER);
//          if (r < 0) break;                 // janela fechada (X) -> encerre
//          if (r == 0) continue;
//          if (ev.type == HAOS_EV_KEY && ev.a == 'q') break;
//      }
//      win_destroy(win);
//
//  Cores são 0xRRGGBB. Coordenadas são em pixels, relativas ao canto
//  superior esquerdo da ÁREA CLIENTE (abaixo da barra de título).
//  O que for desenhado fora do canvas é recortado, sem erro.
//
//  ESPELHA gui/core/userwin.h (kernel): o layout das structs e os
//  números dos eventos precisam ser idênticos.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ---- Eventos (struct haos_win_event::type) ----
#define HAOS_EV_KEY          1   // a = tecla (ASCII ou HAOS_KEY_*)
#define HAOS_EV_MOUSE_DOWN   2   // a,b = x,y na área cliente
#define HAOS_EV_MOUSE_UP     3
#define HAOS_EV_MOUSE_MOVE   4   // a,b = x,y; c = 1 se com o botão pressionado
#define HAOS_EV_MOUSE_SCROLL 5   // a,b = x,y; c = quantidade (> 0 = para cima)
#define HAOS_EV_THEME        7   // o tema do sistema mudou: ui_theme_load() + redesenhe
#define HAOS_EV_RESIZE       6   // a,b = nova largura/altura da área cliente;
                                 // o canvas foi recriado (o que cabia foi
                                 // preservado) — redesenhe o que for necessário

// Teclas especiais (mesmos códigos de kernel/keyboard.h). Observação: Esc
// é consumido pelo desktop e NÃO chega ao programa; Alt+Tab/Alt+F4 idem.
#define HAOS_KEY_UP        0x80
#define HAOS_KEY_DOWN      0x81
#define HAOS_KEY_LEFT      0x82
#define HAOS_KEY_RIGHT     0x83
#define HAOS_KEY_PAGE_UP   0x8E
#define HAOS_KEY_PAGE_DOWN 0x8F
#define HAOS_KEY_HOME      0x90
#define HAOS_KEY_END       0x91

#define WIN_TEXT_OPAQUE    1             // win_text_bg: pinta o fundo de cada caractere
#define WIN_WAIT_FOREVER   0xFFFFFFFFu   // timeout de win_event: espera indefinida

struct haos_win_event {
    uint32_t type;
    int32_t  a, b, c;
};

// ---- Janelas ----

// Cria uma janela com área cliente w x h (ajustada a limites razoáveis; use
// win_size para o tamanho real). Retorna um handle (>= 0) ou -1. No máximo
// 4 janelas por processo.
int win_create(unsigned w, unsigned h, const char* title);

// Fecha a janela. O programa deve parar de usar o handle.
int win_destroy(int win);

// Tamanho atual da área cliente. Retorna 0 ou -1.
int win_size(int win, unsigned* w, unsigned* h);

// ---- Desenho (tudo retorna 0, ou -1 se a janela não existe mais) ----

int win_fill_rect(int win, int x, int y, int w, int h, uint32_t color);

// Texto com a fonte 8x16 do sistema (cada byte é um caractere CP437; '\n'
// desce uma linha). Fundo transparente / opaco. Máx. 255 bytes por chamada.
int win_text(int win, int x, int y, const char* s, uint32_t fg);
int win_text_bg(int win, int x, int y, const char* s, uint32_t fg, uint32_t bg);

// Copia um bloco w*h de pixels 0xRRGGBB (linhas contíguas) para (x, y).
int win_blit(int win, int x, int y, int w, int h, const uint32_t* pixels);

// ---- Título, área de transferência, fonte e tema ----

// Muda o título da janela (até 63 caracteres). 0 ou -1.
int win_set_title(int win, const char* title);

// Área de transferência de TEXTO do sistema (compartilhada com o Editor).
// clip_get copia até `cap` bytes e devolve o tamanho TOTAL guardado (sem NUL);
// clip_get(NULL, 0) só consulta o tamanho. clip_set substitui (até 64 KiB; len 0 limpa).
long clip_get(char* buf, unsigned long cap);
int  clip_set(const char* data, unsigned long len);

// Tabela de glifos do kernel (256 x 16 bytes, 8x16, CP437, bit 7 = pixel mais à esquerda).
// `out` precisa de 4096 bytes. Devolve 4096 ou -1. (haos/draw.h já usa isto.)
int  gfx_font_data(uint8_t* out);

// Cores/métricas do tema do sistema (veja haos/uitheme.h). Escreve `count` uint32_t e
// devolve quantas entradas o kernel conhece, ou -1.
int  gfx_theme_get(uint32_t* out, unsigned count);

// ---- Papel de parede ----

// Define o papel de parede do desktop com w*h pixels 0xRRGGBB (linhas contíguas),
// w <= 1024 e h <= 768 (reduza antes, mantendo a proporção). É uma configuração
// GLOBAL: fica salva e volta no próximo boot. Retorna 0 ou -1.
int gfx_set_wallpaper(const uint32_t* pixels, int w, int h);

// ---- Eventos ----

// Retorna 1 (evento em *ev), 0 (nenhum evento / tempo esgotado) ou -1 (a
// janela não existe mais — fechada pelo usuário: encerre o programa).
// timeout_ms = 0 apenas consulta; WIN_WAIT_FOREVER espera até haver evento.
int win_event(int win, struct haos_win_event* ev, unsigned timeout_ms);

#ifdef __cplusplus
}
#endif
