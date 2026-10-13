// Teste de host do decodificador de imagens (examples/imgview/imgdec.h).
// Usa as imagens de kernel/generated/samples_data.h e os valores esperados
// de tests/imgview/expected.h (ambos gerados por tools/gen_samples.py).
// Compila só este arquivo — não o kernel.
#include <stdio.h>
#include <string.h>
#define IMGDEC_HOST 1
#include "../../examples/imgview/imgdec.h"
#include "../../kernel/generated/samples_data.h"
#include "expected.h"

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)

int main(void) {
    for (unsigned i = 0; i < sizeof(g_expected) / sizeof(g_expected[0]); i++) {
        const ExpectedImg* e = &g_expected[i];
        printf("[%u] %s\n", i + 1, e->name);
        Img im; const char* err = NULL;
        int rc = img_load(e->data, e->size, &im, &err);
        CHECK(rc == 0);
        if (rc != 0) { printf("    erro: %s\n", err ? err : "?"); continue; }
        CHECK(im.w == e->w && im.h == e->h);
        unsigned long long sum = 0;
        for (long k = 0; k < (long)im.w * im.h; k++) sum += im.px[k];
        CHECK(sum == e->sum);
        CHECK(im.px[0] == e->probe[0]);
        CHECK(im.px[im.w - 1] == e->probe[1]);
        CHECK(im.px[(im.h / 2) * im.w + im.w / 2] == e->probe[2]);
        CHECK(im.px[(im.h - 1) * im.w] == e->probe[3]);
        CHECK(im.px[(long)im.w * im.h - 1] == e->probe[4]);
        img_free(&im);
    }

    printf("[erros] entradas invalidas nao podem travar nem estourar\n");
    Img im; const char* err = NULL;
    CHECK(img_load((const uint8_t*)"nada", 4, &im, &err) != 0);
    CHECK(img_load(NULL, 0, &im, &err) != 0);
    // PNG truncado em varios pontos
    const ExpectedImg* e = &g_expected[0];
    for (unsigned cut = 1; cut < e->size; cut += e->size / 23 + 1)
        CHECK(img_load(e->data, cut, &im, &err) != 0);
    // PNG com bytes corrompidos (nao pode quebrar: ok ou erro, nunca crash)
    unsigned char copy[SAMPLE_GRADIENTE_PNG_SIZE];
    for (unsigned pos = 40; pos < e->size; pos += 97) {
        memcpy(copy, e->data, e->size);
        copy[pos] ^= 0x5A;
        if (img_load(copy, e->size, &im, &err) == 0) img_free(&im);
    }
    // BMP truncado
    const ExpectedImg* b = &g_expected[4];
    for (unsigned cut = 2; cut < b->size; cut += b->size / 17 + 1) {
        int rc = img_load(b->data, cut, &im, &err);
        if (rc == 0) img_free(&im);
    }
    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
