// drivers/fb.c — framebuffer 32bpp com shadow buffer (double buffering)
#include "fb.h"
#include "font.h"
#include "../kernel/types.h"
#include "../kernel/memory.h"

static uint32_t* fb_addr  = NULL;  // framebuffer real (VESA/MMIO)
static uint32_t* fb_back  = NULL;  // shadow buffer (RAM, desenho aqui)
static uint32_t* fb_bgcache = NULL; // cache do fundo do desktop
static uint32_t  fb_w     = 0;
static uint32_t  fb_h     = 0;
static uint32_t  fb_pitch = 0;
static uint32_t  fb_size  = 0;    // bytes totais do framebuffer

void fb_init(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch) {
    fb_addr  = (uint32_t*)(uintptr_t)addr;
    fb_w     = w;
    fb_h     = h;
    fb_pitch = pitch;
    fb_size  = h * pitch;
}

void fb_set_backbuffer(void* buf) { fb_back = (uint32_t*)buf; }
void fb_set_bg_cache(void* buf)   { fb_bgcache = (uint32_t*)buf; }

// Flip: copia shadow buffer → framebuffer real (1 memcpy)
void fb_flip(void) {
    if (!fb_back || !fb_addr) return;
    uint64_t* dst = (uint64_t*)fb_addr;
    uint64_t* src = (uint64_t*)fb_back;
    uint32_t n = fb_size / 8;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

// Salva shadow atual como cache do fundo
void fb_save_bg(void) {
    if (!fb_back || !fb_bgcache) return;
    uint64_t* dst = (uint64_t*)fb_bgcache;
    uint64_t* src = (uint64_t*)fb_back;
    uint32_t n = fb_size / 8;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

// Restaura cache do fundo para o shadow buffer
void fb_restore_bg(void) {
    if (!fb_back || !fb_bgcache) return;
    uint64_t* dst = (uint64_t*)fb_back;
    uint64_t* src = (uint64_t*)fb_bgcache;
    uint32_t n = fb_size / 8;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

uint32_t fb_width(void)  { return fb_w; }
uint32_t fb_height(void) { return fb_h; }

// ---- Helper: ponteiro do buffer de escrita ----------------------
static inline uint32_t* fb_target(void) {
    return fb_back ? fb_back : fb_addr;
}

// ---- Blend de cores ---------------------------------------------
static inline uint32_t blend(uint32_t a, uint32_t b, uint32_t t, uint32_t total) {
    if (total == 0) return a;
    uint8_t ar = (a>>16)&0xFF, ag = (a>>8)&0xFF, ab_c = a&0xFF;
    uint8_t br = (b>>16)&0xFF, bg = (b>>8)&0xFF, bb   = b&0xFF;
    uint8_t r = (uint8_t)((int)ar + (int)(br-ar) * (int)t / (int)total);
    uint8_t g = (uint8_t)((int)ag + (int)(bg-ag) * (int)t / (int)total);
    uint8_t bv= (uint8_t)((int)ab_c + (int)(bb-ab_c) * (int)t / (int)total);
    return ((uint32_t)r<<16)|((uint32_t)g<<8)|bv;
}

// ---- Primitivas -------------------------------------------------

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t* buf = fb_target();
    if (x >= fb_w || y >= fb_h) return;
    uint32_t* row = (uint32_t*)((uint8_t*)buf + y * fb_pitch);
    row[x] = color;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t* buf = fb_target();
    uint32_t x2 = x + w; if (x2 > fb_w) x2 = fb_w;
    uint32_t y2 = y + h; if (y2 > fb_h) y2 = fb_h;
    for (uint32_t row = y; row < y2; row++) {
        uint32_t* line = (uint32_t*)((uint8_t*)buf + row * fb_pitch);
        for (uint32_t col = x; col < x2; col++)
            line[col] = color;
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color, uint32_t t) {
    fb_fill_rect(x,         y,         w, t, color);
    fb_fill_rect(x,         y+h-t,     w, t, color);
    fb_fill_rect(x,         y,         t, h, color);
    fb_fill_rect(x+w-t,     y,         t, h, color);
}

void fb_clear(uint32_t color) { fb_fill_rect(0, 0, fb_w, fb_h, color); }

void fb_fill_gradient_h(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t c1, uint32_t c2) {
    uint32_t* buf = fb_target();
    uint32_t x2 = x + w; if (x2 > fb_w) x2 = fb_w;
    uint32_t y2 = y + h; if (y2 > fb_h) y2 = fb_h;
    for (uint32_t col = x; col < x2; col++) {
        uint32_t color = blend(c1, c2, col - x, w);
        for (uint32_t row = y; row < y2; row++) {
            uint32_t* line = (uint32_t*)((uint8_t*)buf + row * fb_pitch);
            line[col] = color;
        }
    }
}

void fb_fill_gradient_v(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t c1, uint32_t c2) {
    uint32_t* buf = fb_target();
    uint32_t x2 = x + w; if (x2 > fb_w) x2 = fb_w;
    uint32_t y2 = y + h; if (y2 > fb_h) y2 = fb_h;
    for (uint32_t row = y; row < y2; row++) {
        uint32_t color = blend(c1, c2, row - y, h);
        uint32_t* line = (uint32_t*)((uint8_t*)buf + row * fb_pitch);
        for (uint32_t col = x; col < x2; col++)
            line[col] = color;
    }
}

void fb_fill_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color) {
    int32_t ir = (int32_t)r;
    int32_t icx = (int32_t)cx, icy = (int32_t)cy;
    for (int32_t dy = -ir; dy <= ir; dy++) {
        for (int32_t dx = -ir; dx <= ir; dx++) {
            if (dx*dx + dy*dy <= ir*ir) {
                int32_t px = icx + dx, py = icy + dy;
                if (px >= 0 && py >= 0)
                    fb_put_pixel((uint32_t)px, (uint32_t)py, color);
            }
        }
    }
}

void fb_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t color, uint32_t r) {
    if (r == 0) { fb_fill_rect(x, y, w, h, color); return; }
    // Centro horizontal
    fb_fill_rect(x+r,   y,       w-r*2, h,       color);
    fb_fill_rect(x,     y+r,     r,     h-r*2,   color);
    fb_fill_rect(x+w-r, y+r,     r,     h-r*2,   color);
    // Cantos via círculos preenchidos (só o quadrante)
    int32_t ir = (int32_t)r;
    for (int32_t dy = 0; dy <= ir; dy++) {
        for (int32_t dx = 0; dx <= ir; dx++) {
            if (dx*dx + dy*dy <= ir*ir) {
                uint32_t pdx = (uint32_t)dx, pdy = (uint32_t)dy;
                fb_put_pixel(x+r-pdx,     y+r-pdy,     color);
                fb_put_pixel(x+w-r+pdx-1, y+r-pdy,     color);
                fb_put_pixel(x+r-pdx,     y+h-r+pdy-1, color);
                fb_put_pixel(x+w-r+pdx-1, y+h-r+pdy-1, color);
            }
        }
    }
}

// Sombra estilada: retângulo escuro com opacidade simulada (stipple)
void fb_draw_shadow(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t x2 = x + w; if (x2 > fb_w) x2 = fb_w;
    uint32_t y2 = y + h; if (y2 > fb_h) y2 = fb_h;
    uint32_t* buf = fb_target();
    for (uint32_t row = y; row < y2; row++) {
        uint32_t* line = (uint32_t*)((uint8_t*)buf + row * fb_pitch);
        uint32_t dist_y = row - y;
        for (uint32_t col = x; col < x2; col++) {
            // Darkens pixel by ~50% using stipple pattern
            uint32_t dist_x = col - x;
            uint32_t dist = dist_x < dist_y ? dist_x : dist_y;
            if (dist < 2 || ((col ^ row) & 1)) {
                uint32_t orig = line[col];
                uint32_t r2 = (orig >> 17) & 0x7F;
                uint32_t g2 = (orig >> 9)  & 0x7F;
                uint32_t b2 = (orig >> 1)  & 0x7F;
                line[col] = (r2<<16)|(g2<<8)|b2;
            }
        }
    }
}

// ---- Texto ------------------------------------------------------

void fb_draw_char(uint32_t x, uint32_t y, uint8_t c,
                  uint32_t fg, uint32_t bg, bool transparent_bg) {
    uint32_t* buf = fb_target();
    const uint8_t* glyph = font_get(c);
    for (int row = 0; row < FONT_H; row++) {
        if (y + row >= fb_h) break;
        uint8_t byte = glyph[row];
        uint32_t* line = (uint32_t*)((uint8_t*)buf + (y + row) * fb_pitch);
        for (int col = 0; col < FONT_W; col++) {
            if (x + col >= fb_w) break;
            if (byte & (1 << (7 - col)))
                line[x + col] = fg;
            else if (!transparent_bg)
                line[x + col] = bg;
        }
    }
}

uint32_t fb_text_width(const char* s) {
    uint32_t n = 0;
    while (*s++) n++;
    return n * FONT_W;
}

void fb_draw_string(uint32_t x, uint32_t y, const char* s,
                    uint32_t fg, uint32_t bg, bool transparent_bg) {
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') { y += FONT_H; cx = x; }
        else if (*s == '\t') { cx += FONT_W * 4; }
        else { fb_draw_char(cx, y, (uint8_t)*s, fg, bg, transparent_bg); cx += FONT_W; }
        s++;
    }
}

void fb_draw_string_centered(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              const char* s, uint32_t fg, uint32_t bg,
                              bool transparent_bg) {
    uint32_t sw = fb_text_width(s);
    uint32_t sx = x + (sw < w ? (w - sw) / 2 : 0);
    uint32_t sy = y + (h > FONT_H ? (h - FONT_H) / 2 : 0);
    fb_draw_string(sx, sy, s, fg, bg, transparent_bg);
}

void fb_scroll_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t lines, uint32_t fill_color) {
    uint32_t bpp  = fb_pitch / fb_w;
    uint32_t lbytes = w * bpp;
    uint32_t skip   = lines * FONT_H;
    uint32_t* buf = fb_target();

    for (uint32_t row = y; row < y + h - skip; row++) {
        uint8_t* dst = (uint8_t*)buf + row * fb_pitch + x * bpp;
        uint8_t* src = (uint8_t*)buf + (row + skip) * fb_pitch + x * bpp;
        for (uint32_t i = 0; i < lbytes; i++) dst[i] = src[i];
    }
    fb_fill_rect(x, y + h - skip, w, skip, fill_color);
}

// ---- Cursor do mouse -------------------------------------------
// Sprite 12x16: 1=branco, 2=preto, 0=transparente
static const uint8_t CURSOR_SPRITE[16][12] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,2,2,2,2,0,0,0},
    {2,1,1,2,1,2,0,0,0,0,0,0},
    {2,1,2,0,2,1,2,0,0,0,0,0},
    {2,2,0,0,0,2,1,2,0,0,0,0},
    {0,0,0,0,0,0,2,1,2,0,0,0},
    {0,0,0,0,0,0,0,2,1,2,0,0},
    {0,0,0,0,0,0,0,0,2,2,0,0},
};

void fb_draw_cursor(uint32_t cx, uint32_t cy) {
    uint32_t* buf = fb_target();
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 12; c++) {
            if (!CURSOR_SPRITE[r][c]) continue;
            uint32_t px = cx + (uint32_t)c;
            uint32_t py = cy + (uint32_t)r;
            if (px >= fb_w || py >= fb_h) continue;
            uint32_t* line = (uint32_t*)((uint8_t*)buf + py * fb_pitch);
            line[px] = (CURSOR_SPRITE[r][c] == 1) ? 0xFFFFFF : 0x000000;
        }
    }
}
