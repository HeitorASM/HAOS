// examples/imgview/imgdec.h — decodificadores de imagem para o HAOS (userland).
//
//   PNG: todos os tipos de cor (cinza, RGB, paleta, cinza+alfa, RGBA), profundidades
//        1/2/4/8/16 bits, filtros 0..4, vários IDAT. NÃO suporta entrelaçado (Adam7).
//   BMP: 1/4/8 bits com paleta, 24 e 32 bits (BI_RGB e BI_BITFIELDS), de baixo para
//        cima ou de cima para baixo. NÃO suporta RLE nem 16 bits.
//
//   Img im; const char* err;
//   if (img_load(dados, tamanho, &im, &err) != 0) ...   // err = mensagem
//   im.px[y * im.w + x]  ->  0xAARRGGBB (alfa NÃO pré-multiplicado)
//   img_free(&im);
//
// Header único, só funções static. Compila no HAOS (libc própria) e no host
// (defina IMGDEC_HOST antes de incluir — usado por tests/imgview).
#pragma once

#ifdef IMGDEC_HOST
#  include <stdint.h>
#  include <stddef.h>
#  include <stdlib.h>
#  include <string.h>
#else
#  include "haos/types.h"
#  include <stdlib.h>
#  include <string.h>
#endif

typedef struct {
    int       w, h;
    uint32_t* px;
} Img;

// Teto de pixels: a heap de um processo é de 32 MiB e precisamos do buffer
// bruto (até 4 bytes/pixel) MAIS os pixels convertidos (4 bytes/pixel).
#define IMG_MAX_PIXELS 3000000u

static uint32_t img_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t img_le32(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static uint32_t img_le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static int img_iabs(int v) { return v < 0 ? -v : v; }

// =============================================================
//  inflate (DEFLATE, RFC 1951) — estrutura do "puff" de Mark Adler
// =============================================================
typedef struct {
    const uint8_t* in; size_t inlen, incnt;
    uint32_t bitbuf; int bitcnt;
    uint8_t* out; size_t outlen, outcnt;
    int bad;                         // acabou a entrada
} InfState;

typedef struct { short count[16]; short symbol[288]; } InfHuff;

static int inf_bits(InfState* s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) { s->bad = 1; return 0; }
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int inf_decode(InfState* s, const InfHuff* h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= inf_bits(s, 1);
        if (s->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;                                       // código inválido
}

static int inf_construct(InfHuff* h, const short* length, int n) {
    short offs[16];
    for (int len = 0; len <= 15; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;                    // sobre-subscrito
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    return left;
}

static const short inf_lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short inf_lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short inf_dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short inf_dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inf_codes(InfState* s, const InfHuff* lencode, const InfHuff* distcode) {
    int symbol;
    do {
        symbol = inf_decode(s, lencode);
        if (symbol < 0) return symbol;
        if (s->bad) return -1;
        if (symbol < 256) {
            if (s->outcnt == s->outlen) return 1;     // saída cheia demais
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            int len = inf_lens[symbol] + inf_bits(s, inf_lext[symbol]);
            symbol = inf_decode(s, distcode);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -10;
            size_t dist = (size_t)inf_dists[symbol] + (size_t)inf_bits(s, inf_dext[symbol]);
            if (s->bad) return -1;
            if (dist > s->outcnt) return -11;
            if (s->outcnt + (size_t)len > s->outlen) return 1;
            while (len--) {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
            }
        }
    } while (symbol != 256);
    return 0;
}

static int inf_stored(InfState* s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return 2;
    unsigned len = s->in[s->incnt++];
    len |= (unsigned)s->in[s->incnt++] << 8;
    unsigned nlen = s->in[s->incnt++];
    nlen |= (unsigned)s->in[s->incnt++] << 8;
    if ((len ^ 0xFFFFu) != nlen) return -2;
    if (s->incnt + len > s->inlen) return 2;
    if (s->outcnt + len > s->outlen) return 1;
    memcpy(s->out + s->outcnt, s->in + s->incnt, len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int inf_fixed(InfState* s) {
    InfHuff lencode, distcode;
    short lengths[288];
    int sym = 0;
    for (; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < 288; sym++) lengths[sym] = 8;
    inf_construct(&lencode, lengths, 288);
    for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
    inf_construct(&distcode, lengths, 30);
    return inf_codes(s, &lencode, &distcode);
}

static int inf_dynamic(InfState* s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    InfHuff lencode, distcode;
    short lengths[320];

    int nlen = inf_bits(s, 5) + 257;
    int ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4;
    if (s->bad) return -1;
    if (nlen > 286 || ndist > 30) return -3;

    int index;
    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)inf_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->bad) return -1;
    if (inf_construct(&lencode, lengths, 19) != 0) return -4;

    index = 0;
    while (index < nlen + ndist) {
        int symbol = inf_decode(s, &lencode);
        if (symbol < 0) return symbol;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + inf_bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + inf_bits(s, 3);
            } else {
                symbol = 11 + inf_bits(s, 7);
            }
            if (s->bad) return -1;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -9;

    int err = inf_construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
    err = inf_construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
    return inf_codes(s, &lencode, &distcode);
}

// Descomprime DEFLATE cru de `in` para `out` (tamanho EXATO esperado: outlen).
// Retorna o nº de bytes gerados, ou -1 se corrompido/curto/longo demais.
static long inf_raw(const uint8_t* in, size_t inlen, uint8_t* out, size_t outlen) {
    InfState s;
    memset(&s, 0, sizeof(s));
    s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen;
    int last;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (s.bad) return -1;
        int err = (type == 0) ? inf_stored(&s) :
                  (type == 1) ? inf_fixed(&s) :
                  (type == 2) ? inf_dynamic(&s) : -1;
        if (err != 0) return -1;
    } while (!last);
    return (long)s.outcnt;
}

// =============================================================
//  PNG
// =============================================================
static int img_paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = img_iabs(p - a), pb = img_iabs(p - b), pc = img_iabs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

// Amostra x de uma linha com profundidade < 8 ou == 8/16 (16: byte alto).
static int img_sample(const uint8_t* r, int x, int depth) {
    if (depth == 8)  return r[x];
    if (depth == 16) return r[2 * x];
    int bit = x * depth;
    return (r[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
}

static int png_load(const uint8_t* d, size_t n, Img* img, const char** err) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static const int chans[7] = {1, 0, 3, 1, 2, 0, 4};
    uint8_t pal[256][4];
    uint8_t* idat = NULL;  size_t idat_len = 0, idat_cap = 0;
    uint8_t* raw = NULL;
    uint32_t* px = NULL;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, interlace = 0, have_ihdr = 0;
    int rc = -1;

    if (n < 8 || memcmp(d, sig, 8) != 0) { *err = "assinatura PNG invalida"; return -1; }
    for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal[i][3] = 255; }

    size_t pos = 8;
    while (pos + 12 <= n) {
        uint32_t len = img_be32(d + pos);
        const uint8_t* type = d + pos + 4;
        const uint8_t* body = d + pos + 8;
        if (len > n - pos - 12) { *err = "PNG truncado"; goto done; }

        if (memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) { *err = "IHDR invalido"; goto done; }
            w = img_be32(body); h = img_be32(body + 4);
            depth = body[8]; ctype = body[9]; interlace = body[12];
            if (body[10] != 0 || body[11] != 0) { *err = "PNG: compressao/filtro desconhecidos"; goto done; }
            have_ihdr = 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            uint32_t cnt = len / 3;
            if (cnt > 256) cnt = 256;
            for (uint32_t i = 0; i < cnt; i++) {
                pal[i][0] = body[3 * i]; pal[i][1] = body[3 * i + 1]; pal[i][2] = body[3 * i + 2];
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            if (ctype == 3)
                for (uint32_t i = 0; i < len && i < 256; i++) pal[i][3] = body[i];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + len > idat_cap) {
                size_t ncap = (idat_len + len) * 2 + 64;
                uint8_t* nb = (uint8_t*)realloc(idat, ncap);
                if (!nb) { *err = "sem memoria"; goto done; }
                idat = nb; idat_cap = ncap;
            }
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + (size_t)len;
    }

    if (!have_ihdr || w == 0 || h == 0) { *err = "PNG sem cabecalho"; goto done; }
    if ((uint64_t)w * h > IMG_MAX_PIXELS) { *err = "imagem grande demais (max ~3 megapixels)"; goto done; }
    if (interlace != 0) { *err = "PNG entrelacado (Adam7) nao suportado"; goto done; }
    {
        int ok = 0;
        switch (ctype) {
            case 0: ok = depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16; break;
            case 2: case 4: case 6: ok = depth == 8 || depth == 16; break;
            case 3: ok = depth == 1 || depth == 2 || depth == 4 || depth == 8; break;
            default: ok = 0;
        }
        if (!ok) { *err = "tipo/profundidade de PNG nao suportado"; goto done; }
    }
    if (!idat || idat_len < 3) { *err = "PNG sem dados"; goto done; }
    if ((idat[0] & 0x0F) != 8 || ((idat[0] << 8) | idat[1]) % 31 != 0 || (idat[1] & 0x20)) {
        *err = "dados zlib invalidos"; goto done;
    }

    {
        int bits = chans[ctype] * depth;
        size_t stride = ((size_t)w * (size_t)bits + 7) / 8;
        size_t rawsize = (size_t)h * (stride + 1);
        raw = (uint8_t*)malloc(rawsize);
        if (!raw) { *err = "sem memoria"; goto done; }
        long got = inf_raw(idat + 2, idat_len - 2, raw, rawsize);
        if (got < 0 || (size_t)got != rawsize) { *err = "dados PNG corrompidos"; goto done; }
        free(idat); idat = NULL;

        // Desfaz os filtros, linha por linha, no próprio buffer.
        int bpp = bits >= 8 ? bits / 8 : 1;
        for (uint32_t y = 0; y < h; y++) {
            uint8_t* row = raw + (size_t)y * (stride + 1);
            int ft = row[0];
            uint8_t* cur = row + 1;
            const uint8_t* prev = y ? raw + (size_t)(y - 1) * (stride + 1) + 1 : NULL;
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)bpp ? cur[i - bpp] : 0;
                int b = prev ? prev[i] : 0;
                int c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
                switch (ft) {
                    case 0: break;
                    case 1: cur[i] = (uint8_t)(cur[i] + a); break;
                    case 2: cur[i] = (uint8_t)(cur[i] + b); break;
                    case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
                    case 4: cur[i] = (uint8_t)(cur[i] + img_paeth(a, b, c)); break;
                    default: *err = "filtro PNG invalido"; goto done;
                }
            }
        }

        px = (uint32_t*)malloc((size_t)w * h * 4);
        if (!px) { *err = "sem memoria"; goto done; }
        int maxv = (depth < 8) ? (1 << depth) - 1 : 255;
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* r = raw + (size_t)y * (stride + 1) + 1;
            for (uint32_t x = 0; x < w; x++) {
                uint32_t R, G, B, A = 255;
                switch (ctype) {
                    case 0: {
                        int v = img_sample(r, (int)x, depth);
                        if (depth < 8) v = v * 255 / maxv;
                        R = G = B = (uint32_t)v;
                        break;
                    }
                    case 2:
                        if (depth == 8) { R = r[3*x]; G = r[3*x+1]; B = r[3*x+2]; }
                        else            { R = r[6*x]; G = r[6*x+2]; B = r[6*x+4]; }
                        break;
                    case 3: {
                        int idx = img_sample(r, (int)x, depth);
                        R = pal[idx][0]; G = pal[idx][1]; B = pal[idx][2]; A = pal[idx][3];
                        break;
                    }
                    case 4:
                        if (depth == 8) { R = G = B = r[2*x]; A = r[2*x+1]; }
                        else            { R = G = B = r[4*x]; A = r[4*x+2]; }
                        break;
                    default:   // 6
                        if (depth == 8) { R = r[4*x]; G = r[4*x+1]; B = r[4*x+2]; A = r[4*x+3]; }
                        else            { R = r[8*x]; G = r[8*x+2]; B = r[8*x+4]; A = r[8*x+6]; }
                        break;
                }
                px[(size_t)y * w + x] = (A << 24) | (R << 16) | (G << 8) | B;
            }
        }
    }
    img->w = (int)w; img->h = (int)h; img->px = px; px = NULL;
    rc = 0;

done:
    if (idat) free(idat);
    if (raw)  free(raw);
    if (px)   free(px);
    return rc;
}

// =============================================================
//  BMP
// =============================================================
static int img_ctz(uint32_t v) { int n = 0; while (v && !(v & 1)) { v >>= 1; n++; } return n; }
static int img_popcount(uint32_t v) { int n = 0; while (v) { n += (int)(v & 1); v >>= 1; } return n; }
static uint32_t img_mask8(uint32_t v, uint32_t mask) {
    if (!mask) return 255;
    int sh = img_ctz(mask), bits = img_popcount(mask);
    uint32_t c = (v & mask) >> sh;
    return bits >= 8 ? (c >> (bits - 8)) : (c * 255 / ((1u << bits) - 1));
}

static int bmp_load(const uint8_t* d, size_t n, Img* img, const char** err) {
    if (n < 26 || d[0] != 'B' || d[1] != 'M') { *err = "assinatura BMP invalida"; return -1; }
    uint32_t off = img_le32(d + 10), hsz = img_le32(d + 14);
    if (hsz < 12 || 14 + (size_t)hsz > n) { *err = "BMP: cabecalho invalido"; return -1; }

    int w, h, bpp, comp = 0;
    uint32_t ncolors = 0;
    if (hsz == 12) {
        w = (int)img_le16(d + 18); h = (int)img_le16(d + 20); bpp = (int)img_le16(d + 24);
    } else {
        if (hsz < 40) { *err = "BMP: cabecalho nao suportado"; return -1; }
        w = (int)(int32_t)img_le32(d + 18); h = (int)(int32_t)img_le32(d + 22);
        bpp = (int)img_le16(d + 28); comp = (int)img_le32(d + 30); ncolors = img_le32(d + 46);
    }
    int top_down = h < 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0) { *err = "BMP: tamanho invalido"; return -1; }
    if ((uint64_t)w * (uint64_t)h > IMG_MAX_PIXELS) { *err = "imagem grande demais (max ~3 megapixels)"; return -1; }
    if (!(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32)) { *err = "BMP: profundidade nao suportada"; return -1; }
    if (comp != 0 && !(comp == 3 && bpp == 32)) { *err = "BMP comprimido nao suportado"; return -1; }

    uint32_t masks[4] = {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0};   // R,G,B,A (BI_RGB 32 bits)
    int use_masks = 0;
    if (comp == 3) {
        if (n < 14 + 40 + 12) { *err = "BMP: mascaras ausentes"; return -1; }
        masks[0] = img_le32(d + 54); masks[1] = img_le32(d + 58); masks[2] = img_le32(d + 62);
        masks[3] = (hsz >= 56 && n >= 14 + 52 + 4) ? img_le32(d + 66) : 0;
        use_masks = 1;
    }

    uint8_t pal[256][3];
    if (bpp <= 8) {
        uint32_t maxc = 1u << bpp;
        uint32_t cnt = ncolors ? ncolors : maxc;
        if (cnt > maxc) cnt = maxc;
        size_t esz = (hsz == 12) ? 3 : 4;
        size_t poff = 14 + (size_t)hsz;
        if (poff + (size_t)cnt * esz > n) { *err = "BMP: paleta truncada"; return -1; }
        for (uint32_t i = 0; i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t* e = d + poff + i * esz;       // B, G, R
            pal[i][0] = e[2]; pal[i][1] = e[1]; pal[i][2] = e[0];
        }
    }

    size_t stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
    if (off > n || stride * (size_t)h > n - off) { *err = "BMP truncado"; return -1; }

    int any_alpha = 0;
    if (bpp == 32 && !use_masks) {
        for (int y = 0; y < h && !any_alpha; y++)
            for (int x = 0; x < w; x++)
                if (d[off + (size_t)y * stride + 4 * (size_t)x + 3]) { any_alpha = 1; break; }
    }

    uint32_t* px = (uint32_t*)malloc((size_t)w * (size_t)h * 4);
    if (!px) { *err = "sem memoria"; return -1; }

    for (int y = 0; y < h; y++) {
        int sy = top_down ? y : (h - 1 - y);
        const uint8_t* r = d + off + (size_t)sy * stride;
        for (int x = 0; x < w; x++) {
            uint32_t R, G, B, A = 255;
            if (bpp == 24) {
                B = r[3 * x]; G = r[3 * x + 1]; R = r[3 * x + 2];
            } else if (bpp == 32) {
                uint32_t v = img_le32(r + 4 * x);
                if (use_masks) {
                    R = img_mask8(v, masks[0]); G = img_mask8(v, masks[1]);
                    B = img_mask8(v, masks[2]); A = masks[3] ? img_mask8(v, masks[3]) : 255;
                } else {
                    B = r[4 * x]; G = r[4 * x + 1]; R = r[4 * x + 2];
                    A = any_alpha ? r[4 * x + 3] : 255;
                }
            } else {
                int idx = img_sample(r, x, bpp);
                R = pal[idx][0]; G = pal[idx][1]; B = pal[idx][2];
            }
            px[(size_t)y * (size_t)w + (size_t)x] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }
    img->w = w; img->h = h; img->px = px;
    return 0;
}

// =============================================================
//  API
// =============================================================
static int img_load(const uint8_t* d, size_t n, Img* img, const char** err) {
    static const char* none;
    if (!err) err = &none;
    img->w = img->h = 0;
    img->px = NULL;
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P') return png_load(d, n, img, err);
    if (n >= 2 && d[0] == 'B' && d[1] == 'M')  return bmp_load(d, n, img, err);
    *err = "formato nao reconhecido (use PNG ou BMP)";
    return -1;
}

static void img_free(Img* img) {
    if (img->px) free(img->px);
    img->px = NULL;
    img->w = img->h = 0;
}
