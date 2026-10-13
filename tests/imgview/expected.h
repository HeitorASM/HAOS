// GERADO por tools/gen_samples.py — não edite à mão.
#pragma once

typedef struct { const char* name; const unsigned char* data; unsigned size;
                 int w, h; unsigned long long sum; unsigned probe[5]; } ExpectedImg;
static const ExpectedImg g_expected[] = {
    { "gradiente.png", sample_gradiente_png, SAMPLE_GRADIENTE_PNG_SIZE, 160, 100, 68584745845208ULL, { 0xff000000u, 0xffff009du, 0xff808080u, 0xff00ff61u, 0xffffffffu } },
    { "circulo.png", sample_circulo_png, SAMPLE_CIRCULO_PNG_SIZE, 128, 128, 39400486068224ULL, { 0x00ff5028u, 0x000150a7u, 0xff7f9068u, 0x00ffcf28u, 0x0001cfa7u } },
    { "paleta.png", sample_paleta_png, SAMPLE_PALETA_PNG_SIZE, 64, 64, 16460604135424ULL, { 0x0000ff00u, 0xfff00f2bu, 0x0000ff00u, 0xfff00f2bu, 0xffe01f06u } },
    { "cinza.png", sample_cinza_png, SAMPLE_CINZA_PNG_SIZE, 64, 48, 13168369728000ULL, { 0xff000000u, 0xfffcfcfcu, 0xffc8c8c8u, 0xff8d8d8du, 0xff898989u } },
    { "tabuleiro.bmp", sample_tabuleiro_bmp, SAMPLE_TABULEIRO_BMP_SIZE, 96, 72, 29629842898944ULL, { 0xff00ff80u, 0xffff0080u, 0xff1e3c8cu, 0xff1e3c8cu, 0xffe6e6e6u } },
};
