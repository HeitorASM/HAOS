#pragma once
#include "types.h"

// ============================================================
//  libc/include/haos/draw.h — renderizador por software para programas Ring 3
//
//  O programa desenha num canvas SEU (um buffer de pixels na heap dele) e
//  manda tudo de uma vez para a janela com cv_present() — UM syscall por quadro,
//  em vez de um por retângulo (win_fill_rect). É a base do toolkit de widgets.
//
//      cv_t cv;
//      cv_init(&cv, 400, 300);
//      cv_fill(&cv, 0, 0, 400, 300, 0x101830);
//      cv_text(&cv, 10, 10, "Ola!", 0xFFFFFF, CV_NO_BG);
//      cv_present(&cv, win);
//
//  Cores 0xRRGGBB. Tudo é recortado ao canvas E ao retângulo de recorte
//  (cv_set_clip), sem erro. A fonte é a do kernel (8x16, CP437), lida uma vez.
// ============================================================

#define CV_FONT_W 8
#define CV_FONT_H 16
#define CV_NO_BG  (-1)          // cv_text: não pinta o fundo dos caracteres

typedef struct {
    uint32_t* px;               // w*h pixels 0xRRGGBB, linhas contíguas
    int w, h;
    int cx0, cy0, cx1, cy1;     // retângulo de recorte [cx0,cx1) x [cy0,cy1)
} cv_t;

// ---- Ciclo de vida ----
int  cv_init(cv_t* c, int w, int h);        // 0 ou -1 (sem memória / tamanho inválido); pinta de preto
int  cv_resize(cv_t* c, int w, int h);      // realoca; o conteúdo é PERDIDO
void cv_free(cv_t* c);

// ---- Recorte ----
void cv_set_clip(cv_t* c, int x, int y, int w, int h);   // interseção com o canvas
void cv_reset_clip(cv_t* c);

// ---- Formas ----
void cv_pixel(cv_t* c, int x, int y, uint32_t color);
void cv_fill(cv_t* c, int x, int y, int w, int h, uint32_t color);
void cv_rect(cv_t* c, int x, int y, int w, int h, uint32_t color);      // borda de 1 px
void cv_hline(cv_t* c, int x, int y, int len, uint32_t color);
void cv_vline(cv_t* c, int x, int y, int len, uint32_t color);
void cv_line(cv_t* c, int x0, int y0, int x1, int y1, uint32_t color);  // qualquer inclinação
void cv_fill_round(cv_t* c, int x, int y, int w, int h, int r, uint32_t color);
void cv_round(cv_t* c, int x, int y, int w, int h, int r, uint32_t color);   // só o contorno (1 px)

// ---- Texto ----
// Desenha `s` (CP437, sem quebra de linha) e devolve a largura em pixels.
// bg = CV_NO_BG deixa o fundo como está.
int  cv_text(cv_t* c, int x, int y, const char* s, uint32_t fg, int64_t bg);
int  cv_text_width(const char* s);          // strlen * CV_FONT_W

// ---- Imagens ----
void cv_blit(cv_t* dst, int x, int y, const uint32_t* src, int sw, int sh);        // 0xRRGGBB, opaco
void cv_blit_alpha(cv_t* dst, int x, int y, const uint32_t* src, int sw, int sh);  // 0xAARRGGBB

// ---- Janela ----
int  cv_present(const cv_t* c, int win);    // win_blit do canvas inteiro; 0 ou -1
