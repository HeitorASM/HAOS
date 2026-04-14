#include "utf8cp437.h"
#include "../kernel/types.h"

// Mapeamento dos pontos de código Unicode mais usados no HAOS para índices CP437
typedef struct {
    uint32_t unicode;
    uint8_t  cp437;
} unicode_to_cp437_t;

static const unicode_to_cp437_t map[] = {
    // Box drawing (leve e pesado)
    { 0x2500, 0xC4 },  // ─
    { 0x2502, 0xB3 },  // │
    { 0x250C, 0xDA },  // ┌
    { 0x2510, 0xBF },  // ┐
    { 0x2514, 0xC0 },  // └
    { 0x2518, 0xD9 },  // ┘
    { 0x251C, 0xC3 },  // ├
    { 0x2524, 0xB4 },  // ┤
    { 0x252C, 0xC2 },  // ┬
    { 0x2534, 0xC1 },  // ┴
    { 0x253C, 0xC5 },  // ┼

    // Box drawing duplo (usado no banner do HAOS)
    { 0x2550, 0xCD },  // ═
    { 0x2551, 0xBA },  // ║
    { 0x2554, 0xC9 },  // ╔
    { 0x2557, 0xBB },  // ╗
    { 0x255A, 0xC8 },  // ╚
    { 0x255D, 0xBC },  // ╝
    { 0x2560, 0xCC },  // ╠
    { 0x2563, 0xB9 },  // ╣
    { 0x2566, 0xCB },  // ╦
    { 0x2569, 0xCA },  // ╩
    { 0x256C, 0xCE },  // ╬

    // Blocos e sombras
    { 0x2588, 0xDB },  // █ full block
    { 0x2584, 0xDC },  // ▄ lower half block
    { 0x2580, 0xDD },  // ▀ upper half block
    { 0x2591, 0xB0 },  // ░ light shade
    { 0x2592, 0xB1 },  // ▒ medium shade
    { 0x2593, 0xB2 },  // ▓ dark shade

    // Acentos portugueses (pré‑compostos)
    { 0x00C0, 0x85 },  // À
    { 0x00C1, 0xA0 },  // Á
    { 0x00C2, 0x83 },  // Â
    { 0x00C3, 0xA5 },  // Ã
    { 0x00C7, 0x80 },  // Ç
    { 0x00C8, 0x8A },  // È
    { 0x00C9, 0x90 },  // É
    { 0x00CA, 0x88 },  // Ê
    { 0x00CD, 0xA1 },  // Í
    { 0x00D3, 0xA2 },  // Ó
    { 0x00D4, 0x93 },  // Ô
    { 0x00D5, 0xA4 },  // Õ
    { 0x00DA, 0xA3 },  // Ú
    { 0x00E0, 0x85 },  // à
    { 0x00E1, 0xA0 },  // á
    { 0x00E2, 0x83 },  // â
    { 0x00E3, 0xA4 },  // ã
    { 0x00E7, 0x87 },  // ç
    { 0x00E8, 0x8A },  // è
    { 0x00E9, 0x82 },  // é
    { 0x00EA, 0x88 },  // ê
    { 0x00ED, 0xA1 },  // í
    { 0x00F3, 0xA2 },  // ó
    { 0x00F4, 0x93 },  // ô
    { 0x00F5, 0xA4 },  // õ
    { 0x00FA, 0xA3 },  // ú
    { 0x00FC, 0x81 },  // ü

    // Outros símbolos úteis
    { 0x00B0, 0xF8 },  // °
    { 0x00B1, 0xF1 },  // ±
    { 0x00B7, 0xFA },  // ·
    { 0x03A9, 0xEA },  // Ω
    { 0x221E, 0xEC },  // ∞
    { 0x2264, 0xF3 },  // ≤
    { 0x2265, 0xF2 },  // ≥
    { 0x221A, 0xFB },  // √
};

#define MAP_SIZE (sizeof(map) / sizeof(map[0]))

// Retorna o byte CP437 correspondente ao ponto de código Unicode,
// ou '?' se não houver mapeamento.
static uint8_t unicode_to_cp437(uint32_t cp) {
    // ASCII direto
    if (cp < 0x80) return (uint8_t)cp;

    // CP437 estendido (0x80–0xFF) é mapeado diretamente em muitos casos,
    // mas alguns caracteres Unicode correspondem exatamente ao índice CP437.
    // Exemplo: U+00C7 (Ç) = 0x80 em CP437.
    // Preferimos usar nossa tabela explícita para evitar confusão.
    for (size_t i = 0; i < MAP_SIZE; i++) {
        if (map[i].unicode == cp)
            return map[i].cp437;
    }
    return '?';
}

// Decodifica uma sequência UTF‑8 a partir de `*s`, avança o ponteiro
// e retorna o byte CP437 correspondente.
uint8_t utf8_to_cp437(const char** s) {
    const uint8_t* p = (const uint8_t*)*s;
    uint32_t cp = 0;
    uint8_t c = *p;

    if (c < 0x80) {
        // ASCII
        (*s)++;
        return c;
    }

    // Determina número de bytes da sequência
    int len = 0;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        cp = c & 0x07;
    } else {
        // Sequência inválida, trata como '?'
        (*s)++;
        return '?';
    }

    // Lê os bytes seguintes
    for (int i = 1; i < len; i++) {
        p++;
        if ((*p & 0xC0) != 0x80) {
            // Erro, aborta
            (*s) += i;
            return '?';
        }
        cp = (cp << 6) | (*p & 0x3F);
    }

    p++;
    *s = (const char*)p;
    return unicode_to_cp437(cp);
}