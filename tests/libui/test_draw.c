// Teste de host do renderizador de apps (libc/src/gfx/draw.c). Compila só
// draw.c + este arquivo; a fonte e o win_blit do kernel são substituídos por stubs.
#include <stdio.h>
#include <string.h>
#include "haos/draw.h"
#include "haos/gfx.h"

static int g_pw, g_ph, g_pwin;
int gfx_font_data(uint8_t* out) {
    memset(out, 0, 4096);
    out['A' * 16 + 0] = 0x80;       // 'A': linha 0 = só o pixel mais à ESQUERDA
    out['A' * 16 + 1] = 0x01;       //      linha 1 = só o pixel mais à DIREITA
    out['B' * 16 + 0] = 0xFF;       // 'B': linha 0 cheia
    return 4096;
}
int win_blit(int win, int x, int y, int w, int h, const uint32_t* px) {
    (void)x; (void)y; (void)px;
    g_pwin = win; g_pw = w; g_ph = h;
    return 0;
}

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)
#define PX(cv, x, y) ((cv).px[(size_t)(y) * (size_t)(cv).w + (size_t)(x)])

int main(void) {
    cv_t cv;

    printf("[1] init / resize / tamanhos invalidos\n");
    CHECK(cv_init(&cv, 40, 30) == 0 && cv.px && cv.w == 40 && cv.h == 30);
    CHECK(PX(cv, 0, 0) == 0 && PX(cv, 39, 29) == 0);
    CHECK(cv.cx0 == 0 && cv.cy0 == 0 && cv.cx1 == 40 && cv.cy1 == 30);
    cv_t bad;
    CHECK(cv_init(&bad, 0, 10) == -1 && cv_init(&bad, 10, -1) == -1 && cv_init(&bad, 5000, 10) == -1);

    printf("[2] cv_fill (mascara a cor para 24 bits)\n");
    cv_fill(&cv, 5, 5, 10, 10, 0xFFFF0000);
    CHECK(PX(cv, 5, 5) == 0xFF0000 && PX(cv, 14, 14) == 0xFF0000);
    CHECK(PX(cv, 15, 15) == 0 && PX(cv, 4, 5) == 0 && PX(cv, 5, 4) == 0);

    printf("[3] recorte\n");
    cv_set_clip(&cv, 20, 20, 4, 4);
    cv_fill(&cv, 0, 0, 100, 100, 0x00FF00);
    CHECK(PX(cv, 20, 20) == 0x00FF00 && PX(cv, 23, 23) == 0x00FF00);
    CHECK(PX(cv, 19, 20) == 0 && PX(cv, 24, 20) == 0 && PX(cv, 20, 24) == 0);
    cv_set_clip(&cv, -50, -50, 1000, 1000);                       // maior que o canvas: vira o canvas
    CHECK(cv.cx0 == 0 && cv.cy0 == 0 && cv.cx1 == 40 && cv.cy1 == 30);
    cv_reset_clip(&cv);

    printf("[4] coordenadas negativas / fora do canvas\n");
    cv_fill(&cv, -5, -5, 10, 10, 0x0000FF);
    CHECK(PX(cv, 0, 0) == 0x0000FF && PX(cv, 4, 4) == 0x0000FF && PX(cv, 5, 5) == 0xFF0000);   // (5,5) segue vermelho do [2]
    cv_fill(&cv, 35, 25, 100, 100, 0x0000FF);
    CHECK(PX(cv, 39, 29) == 0x0000FF);
    cv_pixel(&cv, -1, 0, 0xFFFFFF); cv_pixel(&cv, 0, 99, 0xFFFFFF);   // não pode quebrar

    printf("[5] cv_rect (so a borda)\n");
    cv_free(&cv); cv_init(&cv, 40, 30);
    cv_rect(&cv, 10, 10, 6, 5, 0xAAAAAA);
    CHECK(PX(cv, 10, 10) == 0xAAAAAA && PX(cv, 15, 10) == 0xAAAAAA);
    CHECK(PX(cv, 10, 14) == 0xAAAAAA && PX(cv, 15, 14) == 0xAAAAAA);
    CHECK(PX(cv, 12, 12) == 0 && PX(cv, 11, 11) == 0);

    printf("[6] cv_line\n");
    cv_free(&cv); cv_init(&cv, 40, 30);
    cv_line(&cv, 0, 0, 9, 9, 0x123456);
    CHECK(PX(cv, 0, 0) == 0x123456 && PX(cv, 5, 5) == 0x123456 && PX(cv, 9, 9) == 0x123456);
    CHECK(PX(cv, 5, 6) == 0);
    cv_line(&cv, 20, 3, 30, 3, 0x654321);                         // horizontal
    CHECK(PX(cv, 20, 3) == 0x654321 && PX(cv, 30, 3) == 0x654321 && PX(cv, 31, 3) == 0);
    cv_line(&cv, 9, 20, 0, 11, 0x111111);                         // da direita para a esquerda
    CHECK(PX(cv, 9, 20) == 0x111111 && PX(cv, 0, 11) == 0x111111);

    printf("[7] cv_text\n");
    cv_free(&cv); cv_init(&cv, 40, 30);
    CHECK(cv_text(&cv, 0, 0, "A", 0xFFFFFF, CV_NO_BG) == 8);
    CHECK(PX(cv, 0, 0) == 0xFFFFFF && PX(cv, 7, 1) == 0xFFFFFF);
    CHECK(PX(cv, 1, 0) == 0 && PX(cv, 0, 1) == 0 && PX(cv, 6, 1) == 0);
    CHECK(cv_text(&cv, 0, 16, "AB", 0xFF00FF, 0x112233) == 16);
    CHECK(PX(cv, 0, 16) == 0xFF00FF && PX(cv, 1, 16) == 0x112233);     // bg pintado
    CHECK(PX(cv, 8, 16) == 0xFF00FF && PX(cv, 15, 16) == 0xFF00FF);    // 'B' linha 0 cheia
    CHECK(cv_text_width("hello") == 40 && cv_text_width("") == 0 && cv_text_width(NULL) == 0);
    cv_set_clip(&cv, 0, 0, 4, 30);                                // texto cortado pelo recorte
    memset(cv.px, 0, 40 * 30 * 4);
    CHECK(cv_text(&cv, 0, 0, "BB", 0xFFFFFF, CV_NO_BG) == 16);          // largura total mesmo cortado
    CHECK(PX(cv, 3, 0) == 0xFFFFFF && PX(cv, 4, 0) == 0 && PX(cv, 9, 0) == 0);
    cv_reset_clip(&cv);

    printf("[8] cantos arredondados\n");
    cv_free(&cv); cv_init(&cv, 40, 30);
    cv_fill_round(&cv, 0, 0, 20, 20, 5, 0xFFFFFF);
    CHECK(PX(cv, 0, 0) == 0 && PX(cv, 19, 0) == 0 && PX(cv, 0, 19) == 0 && PX(cv, 19, 19) == 0);
    CHECK(PX(cv, 3, 0) == 0xFFFFFF && PX(cv, 10, 0) == 0xFFFFFF && PX(cv, 0, 10) == 0xFFFFFF);
    CHECK(PX(cv, 10, 10) == 0xFFFFFF && PX(cv, 19, 10) == 0xFFFFFF && PX(cv, 10, 19) == 0xFFFFFF);
    cv_free(&cv); cv_init(&cv, 40, 30);
    cv_round(&cv, 0, 0, 20, 20, 5, 0xFFFFFF);
    CHECK(PX(cv, 10, 0) == 0xFFFFFF && PX(cv, 0, 10) == 0xFFFFFF && PX(cv, 19, 10) == 0xFFFFFF);
    CHECK(PX(cv, 10, 10) == 0 && PX(cv, 0, 0) == 0 && PX(cv, 19, 19) == 0);
    cv_fill_round(&cv, 25, 5, 6, 6, 100, 0x00FF00);                // raio absurdo: limitado a min(w,h)/2
    CHECK(PX(cv, 28, 8) == 0x00FF00);

    printf("[9] blit opaco e recortado\n");
    cv_free(&cv); cv_init(&cv, 40, 30);
    uint32_t src[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    cv_blit(&cv, 3, 3, src, 2, 2);
    CHECK(PX(cv, 3, 3) == 0x111111 && PX(cv, 4, 3) == 0x222222 && PX(cv, 3, 4) == 0x333333 && PX(cv, 4, 4) == 0x444444);
    cv_blit(&cv, -1, -1, src, 2, 2);                               // só o pixel 4 (0x444444) cai em (0,0)
    CHECK(PX(cv, 0, 0) == 0x444444 && PX(cv, 1, 0) == 0);
    cv_blit(&cv, 39, 29, src, 2, 2);
    CHECK(PX(cv, 39, 29) == 0x111111);

    printf("[10] blit com alfa\n");
    cv_free(&cv); cv_init(&cv, 8, 8);
    cv_fill(&cv, 0, 0, 8, 8, 0x000000);
    uint32_t a[3] = { 0x80FF0000, 0x00FFFFFF, 0xFF00FF00 };
    cv_blit_alpha(&cv, 0, 0, a, 3, 1);
    CHECK(PX(cv, 0, 0) == 0x800000);                               // 50% de vermelho sobre preto
    CHECK(PX(cv, 1, 0) == 0x000000);                               // alfa 0: não muda
    CHECK(PX(cv, 2, 0) == 0x00FF00);                               // alfa 255: substitui

    printf("[11] cv_present e resize\n");
    CHECK(cv_present(&cv, 7) == 0 && g_pwin == 7 && g_pw == 8 && g_ph == 8);
    CHECK(cv_resize(&cv, 16, 4) == 0 && cv.w == 16 && cv.h == 4 && PX(cv, 0, 0) == 0);
    CHECK(cv_resize(&cv, 0, 4) == -1 && cv.w == 16);               // falha não estraga o canvas
    cv_free(&cv);
    CHECK(cv.px == NULL && cv_present(&cv, 1) == -1);

    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
