// examples/winpaint/main.c — mini editor de desenho em janela própria.
//
// Mostra o ciclo completo das syscalls de janela: win_create, desenho
// (win_fill_rect / win_text_bg), laço de eventos (win_event) e como
// reagir a mouse, teclado, redimensionamento e ao fechamento da janela.
//
//   run winpaint.hexec
//
//   - arraste com o botão esquerdo dentro da área escura para desenhar
//   - clique numa cor da barra de cima (ou tecle 1..8) para trocar de cor
//   - c = limpar, q = sair (Esc é do desktop e não chega ao programa)
//   - fechar a janela no X ou redimensioná-la também funciona

#include "haos/gfx.h"

#define TOOLBAR_H 28
#define SWATCH    20
#define NCOLORS   8
#define BG_PAINT  0x0F1220
#define BG_BAR    0x1A2238

static const uint32_t palette[NCOLORS] = {
    0xFFFFFF, 0xFF4444, 0xFFAA33, 0xFFEE44, 0x44CC44, 0x44CCFF, 0x6677FF, 0xCC66FF
};

static int      win;
static unsigned cw, ch;          // tamanho atual da área cliente
static int      cur = 0;         // cor selecionada
static int      drawing = 0;
static int      last_x, last_y;

static void draw_toolbar(void) {
    win_fill_rect(win, 0, 0, (int)cw, TOOLBAR_H, BG_BAR);
    for (int i = 0; i < NCOLORS; i++) {
        int x = 6 + i * (SWATCH + 6);
        if (i == cur) win_fill_rect(win, x - 2, 2, SWATCH + 4, SWATCH + 4, 0xFFFFFF);
        win_fill_rect(win, x, 4, SWATCH, SWATCH, palette[i]);
    }
    win_text_bg(win, 6 + NCOLORS * (SWATCH + 6) + 8, 6, "c: limpar   q: sair",
                0xAABBCC, BG_BAR);
}

static void clear_paint(void) {
    win_fill_rect(win, 0, TOOLBAR_H, (int)cw, (int)ch - TOOLBAR_H, BG_PAINT);
}

static void dot(int x, int y) {
    if (y < TOOLBAR_H + 1) return;
    win_fill_rect(win, x - 1, y - 1, 3, 3, palette[cur]);
}

// Liga dois pontos com uma sequência de "pincéis" para o traço não
// ficar tracejado quando o mouse se move rápido.
static void line(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;
    if (steps == 0) { dot(x0, y0); return; }
    for (int i = 0; i <= steps; i++) dot(x0 + dx * i / steps, y0 + dy * i / steps);
}

int main(void) {
    win = win_create(420, 300, "HAOS Paint");
    if (win < 0) return 1;                       // sem espaço no WM ou sem memória
    if (win_size(win, &cw, &ch) != 0) return 1;  // tamanho REAL (o kernel pode ajustar)

    clear_paint();
    draw_toolbar();

    struct haos_win_event ev;
    for (;;) {
        int r = win_event(win, &ev, WIN_WAIT_FOREVER);
        if (r < 0) return 0;                     // o usuário fechou a janela
        if (r == 0) continue;

        switch (ev.type) {
        case HAOS_EV_KEY:
            if (ev.a == 'q') { win_destroy(win); return 0; }
            if (ev.a == 'c') clear_paint();
            if (ev.a >= '1' && ev.a < '1' + NCOLORS) { cur = ev.a - '1'; draw_toolbar(); }
            break;

        case HAOS_EV_MOUSE_DOWN:
            if (ev.b < TOOLBAR_H) {              // clique na barra: escolhe cor
                int i = (ev.a - 6) / (SWATCH + 6);
                if (ev.a >= 6 && i >= 0 && i < NCOLORS) { cur = i; draw_toolbar(); }
            } else {
                drawing = 1;
                last_x = ev.a; last_y = ev.b;
                dot(ev.a, ev.b);
            }
            break;

        case HAOS_EV_MOUSE_MOVE:
            if (drawing && ev.c) {               // c = 1: botão pressionado
                line(last_x, last_y, ev.a, ev.b);
                last_x = ev.a; last_y = ev.b;
            }
            break;

        case HAOS_EV_MOUSE_UP:
            drawing = 0;
            break;

        case HAOS_EV_RESIZE:
            // O kernel preservou o desenho no que coube; a área nova vem
            // com o fundo padrão. Só precisamos ajustar a barra.
            cw = (unsigned)ev.a;
            ch = (unsigned)ev.b;
            draw_toolbar();
            break;
        }
    }
}
