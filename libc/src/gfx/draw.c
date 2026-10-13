#include "haos/draw.h"
#include "haos/gfx.h"
#include <stdlib.h>
#include <string.h>

// ---------------- fonte ----------------
static uint8_t g_font[256 * CV_FONT_H];
static int     g_font_loaded;

static void font_load(void) {
    if (g_font_loaded) return;
    memset(g_font, 0, sizeof(g_font));
    gfx_font_data(g_font);               // se falhar, ficam glifos em branco
    g_font_loaded = 1;
}

// ---------------- ciclo de vida ----------------
int cv_init(cv_t* c, int w, int h) {
    if (!c) return -1;
    c->px = NULL; c->w = c->h = 0;
    c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0;
    return cv_resize(c, w, h);
}

int cv_resize(cv_t* c, int w, int h) {
    if (!c || w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    uint32_t* np = (uint32_t*)malloc((size_t)w * (size_t)h * 4);
    if (!np) return -1;
    memset(np, 0, (size_t)w * (size_t)h * 4);
    if (c->px) free(c->px);
    c->px = np; c->w = w; c->h = h;
    cv_reset_clip(c);
    return 0;
}

void cv_free(cv_t* c) {
    if (!c) return;
    if (c->px) free(c->px);
    c->px = NULL; c->w = c->h = 0;
    c->cx0 = c->cy0 = c->cx1 = c->cy1 = 0;
}

// ---------------- recorte ----------------
void cv_reset_clip(cv_t* c) {
    c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h;
}

void cv_set_clip(cv_t* c, int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    if (x1 < x) x1 = x;
    if (y1 < y) y1 = y;
    c->cx0 = x; c->cy0 = y; c->cx1 = x1; c->cy1 = y1;
}

// ---------------- formas ----------------
void cv_pixel(cv_t* c, int x, int y, uint32_t color) {
    if (!c->px || x < c->cx0 || x >= c->cx1 || y < c->cy0 || y >= c->cy1) return;
    c->px[(size_t)y * (size_t)c->w + (size_t)x] = color & 0xFFFFFF;
}

void cv_fill(cv_t* c, int x, int y, int w, int h, uint32_t color) {
    if (!c->px || w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (x < c->cx0) x = c->cx0;
    if (y < c->cy0) y = c->cy0;
    if (x1 > c->cx1) x1 = c->cx1;
    if (y1 > c->cy1) y1 = c->cy1;
    if (x >= x1 || y >= y1) return;
    color &= 0xFFFFFF;
    for (int yy = y; yy < y1; yy++) {
        uint32_t* row = c->px + (size_t)yy * (size_t)c->w;
        for (int xx = x; xx < x1; xx++) row[xx] = color;
    }
}

void cv_hline(cv_t* c, int x, int y, int len, uint32_t color) { cv_fill(c, x, y, len, 1, color); }
void cv_vline(cv_t* c, int x, int y, int len, uint32_t color) { cv_fill(c, x, y, 1, len, color); }

void cv_rect(cv_t* c, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    cv_hline(c, x, y, w, color);
    if (h > 1) cv_hline(c, x, y + h - 1, w, color);
    if (h > 2) {
        cv_vline(c, x, y + 1, h - 2, color);
        if (w > 1) cv_vline(c, x + w - 1, y + 1, h - 2, color);
    }
}

static int iabs_(int v) { return v < 0 ? -v : v; }

void cv_line(cv_t* c, int x0, int y0, int x1, int y1, uint32_t color) {   // Bresenham
    int dx = iabs_(x1 - x0), dy = -iabs_(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        cv_pixel(c, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// O canto é um quarto de círculo de raio r com centro em (r, r) a partir do canto
// do retângulo. O pixel (xx, yy) do canto superior esquerdo (0 <= xx,yy < r) está
// dentro se o CENTRO dele cai no círculo — conta em "dobros" para ficar em inteiros.
static int corner_inside(int xx, int yy, int r) {
    long dx2 = 2L * (r - xx) - 1, dy2 = 2L * (r - yy) - 1;
    return dx2 * dx2 + dy2 * dy2 <= 4L * r * r;
}

static int clamp_radius(int w, int h, int r) {
    int m = (w < h ? w : h) / 2;
    return r > m ? m : (r < 0 ? 0 : r);
}

void cv_fill_round(cv_t* c, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    r = clamp_radius(w, h, r);
    if (r <= 1) { cv_fill(c, x, y, w, h, color); return; }
    cv_fill(c, x, y + r, w, h - 2 * r, color);                       // faixa central
    for (int yy = 0; yy < r; yy++) {
        int inset = r;
        for (int xx = 0; xx < r; xx++)
            if (corner_inside(xx, yy, r)) { inset = xx; break; }
        int len = w - 2 * inset;
        if (len <= 0) continue;
        cv_fill(c, x + inset, y + yy, len, 1, color);                // topo
        cv_fill(c, x + inset, y + h - 1 - yy, len, 1, color);        // base (espelhado)
    }
}

void cv_round(cv_t* c, int x, int y, int w, int h, int r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    r = clamp_radius(w, h, r);
    if (r <= 1) { cv_rect(c, x, y, w, h, color); return; }
    cv_hline(c, x + r, y, w - 2 * r, color);
    cv_hline(c, x + r, y + h - 1, w - 2 * r, color);
    cv_vline(c, x, y + r, h - 2 * r, color);
    cv_vline(c, x + w - 1, y + r, h - 2 * r, color);
    for (int yy = 0; yy < r; yy++)
        for (int xx = 0; xx < r; xx++) {
            if (!corner_inside(xx, yy, r)) continue;
            int edge = (xx == 0 || yy == 0 || !corner_inside(xx - 1, yy, r) ||
                        !corner_inside(xx, yy - 1, r));
            if (!edge) continue;
            cv_pixel(c, x + xx,         y + yy,         color);
            cv_pixel(c, x + w - 1 - xx, y + yy,         color);
            cv_pixel(c, x + xx,         y + h - 1 - yy, color);
            cv_pixel(c, x + w - 1 - xx, y + h - 1 - yy, color);
        }
}

// ---------------- texto ----------------
int cv_text_width(const char* s) { return s ? (int)strlen(s) * CV_FONT_W : 0; }

int cv_text(cv_t* c, int x, int y, const char* s, uint32_t fg, int64_t bg) {
    if (!s) return 0;
    font_load();
    int start = x;
    for (; *s; s++, x += CV_FONT_W) {
        if (x >= c->cx1) { x += CV_FONT_W * (int)strlen(s + 1) + CV_FONT_W; break; }   // resto fora: só avança
        if (x + CV_FONT_W <= c->cx0 || y >= c->cy1 || y + CV_FONT_H <= c->cy0) continue;
        const uint8_t* g = g_font + (unsigned)(uint8_t)*s * CV_FONT_H;
        for (int row = 0; row < CV_FONT_H; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < CV_FONT_W; col++) {
                if (bits & (0x80u >> col)) cv_pixel(c, x + col, y + row, fg);
                else if (bg != CV_NO_BG)   cv_pixel(c, x + col, y + row, (uint32_t)bg);
            }
        }
    }
    return x - start;
}

// ---------------- imagens ----------------
void cv_blit(cv_t* dst, int x, int y, const uint32_t* src, int sw, int sh) {
    if (!dst->px || !src || sw <= 0 || sh <= 0) return;
    int x0 = x < dst->cx0 ? dst->cx0 : x, y0 = y < dst->cy0 ? dst->cy0 : y;
    int x1 = x + sw > dst->cx1 ? dst->cx1 : x + sw, y1 = y + sh > dst->cy1 ? dst->cy1 : y + sh;
    for (int yy = y0; yy < y1; yy++) {
        const uint32_t* s = src + (size_t)(yy - y) * (size_t)sw + (size_t)(x0 - x);
        uint32_t* d = dst->px + (size_t)yy * (size_t)dst->w + (size_t)x0;
        for (int xx = x0; xx < x1; xx++) *d++ = *s++ & 0xFFFFFF;
    }
}

void cv_blit_alpha(cv_t* dst, int x, int y, const uint32_t* src, int sw, int sh) {
    if (!dst->px || !src || sw <= 0 || sh <= 0) return;
    int x0 = x < dst->cx0 ? dst->cx0 : x, y0 = y < dst->cy0 ? dst->cy0 : y;
    int x1 = x + sw > dst->cx1 ? dst->cx1 : x + sw, y1 = y + sh > dst->cy1 ? dst->cy1 : y + sh;
    for (int yy = y0; yy < y1; yy++) {
        const uint32_t* s = src + (size_t)(yy - y) * (size_t)sw + (size_t)(x0 - x);
        uint32_t* d = dst->px + (size_t)yy * (size_t)dst->w + (size_t)x0;
        for (int xx = x0; xx < x1; xx++, s++, d++) {
            uint32_t p = *s, a = p >> 24;
            if (a == 255) { *d = p & 0xFFFFFF; continue; }
            if (a == 0) continue;
            uint32_t q = *d;
            uint32_t r = (((p >> 16) & 255) * a + ((q >> 16) & 255) * (255 - a)) / 255;
            uint32_t g = (((p >> 8) & 255) * a + ((q >> 8) & 255) * (255 - a)) / 255;
            uint32_t b = ((p & 255) * a + (q & 255) * (255 - a)) / 255;
            *d = (r << 16) | (g << 8) | b;
        }
    }
}

// ---------------- janela ----------------
int cv_present(const cv_t* c, int win) {
    if (!c || !c->px) return -1;
    return win_blit(win, 0, 0, c->w, c->h, c->px);
}
