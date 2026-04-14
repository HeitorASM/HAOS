#pragma once
#include "../kernel/types.h"

// ---- Paleta de cores (formato 0x00RRGGBB) ----------------------
#define COLOR_BLACK        0x000000
#define COLOR_WHITE        0xFFFFFF
#define COLOR_RED          0xFF4444
#define COLOR_GREEN        0x44CC44
#define COLOR_BLUE         0x2266CC

// Paleta principal do HAOS (redesenhada para look moderno)
#define COLOR_BG           0x0B0E1A   // fundo da área de trabalho
#define COLOR_BG2          0x111828   // superfície secundária
#define COLOR_ACCENT       0x5AA0FF   // azul elétrico
#define COLOR_ACCENT_DARK  0x2A6AC8
#define COLOR_ACCENT_GLOW  0x3070CC
#define COLOR_TASKBAR      0x07091200 // cor base da taskbar (depois misturada)
#define COLOR_TASKBAR_BG   0x090C18
#define COLOR_TASKBAR_LINE 0x2244AA
#define COLOR_TITLEBAR     0x1A3A70   // barra de título ativa
#define COLOR_TITLEBAR_HI  0x2A5AAA   // destaque superior
#define COLOR_TITLEBAR_INA 0x2A2A44   // barra de título inativa
#define COLOR_WIN_BG       0x0F1220   // fundo de janela (dark mode)
#define COLOR_WIN_BORDER   0x2A4A8A
#define COLOR_WIN_BORDER_A 0x4A7ACC   // borda ativa
#define COLOR_TEXT_DARK    0x1A1A2E
#define COLOR_TEXT_LIGHT   0xDDEEFF
#define COLOR_TEXT_GRAY    0x7799BB
#define COLOR_TEXT_DIM     0x445566
#define COLOR_BOOT_BG      0x060810
#define COLOR_PROGRESS_BG  0x111828
#define COLOR_PROGRESS_FG  0x5AA0FF
#define COLOR_TERM_BG      0x0D1117
#define COLOR_TERM_FG      0x00FF99
#define COLOR_TERM_PROMPT  0x5AA0FF
#define COLOR_TERM_CMD     0xFFDD44
#define COLOR_CLOSE_BTN    0xE05555
#define COLOR_MIN_BTN      0xE0B840
#define COLOR_MAX_BTN      0x50B050
#define COLOR_ICON_BG      0x131926
#define COLOR_ICON_BORDER  0x2A4A7A
#define COLOR_SEPARATOR    0x1A2840
#define COLOR_SELECTED     0x1A3A6A
#define COLOR_HOVER        0x162030

// ---- Inicialização -----------------------------------------------
void fb_init(uint64_t addr, uint32_t w, uint32_t h, uint32_t pitch);
void fb_set_backbuffer(void* buf);   // define shadow buffer
void fb_flip(void);                  // copia shadow → framebuffer real
void fb_set_bg_cache(void* buf);     // define cache do fundo do desktop

uint32_t fb_width(void);
uint32_t fb_height(void);

// ---- Primitivas de desenho --------------------------------------
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color, uint32_t thickness);
void fb_clear(uint32_t color);
void fb_fill_gradient_h(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t c1, uint32_t c2);
void fb_fill_gradient_v(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t c1, uint32_t c2);
void fb_fill_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color);
void fb_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t color, uint32_t r);
void fb_draw_shadow(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

// ---- Texto ------------------------------------------------------
void     fb_draw_char(uint32_t x, uint32_t y, uint8_t c,
                      uint32_t fg, uint32_t bg, bool transparent_bg);
uint32_t fb_text_width(const char* s);
void     fb_draw_string(uint32_t x, uint32_t y, const char* s,
                        uint32_t fg, uint32_t bg, bool transparent_bg);
void     fb_draw_string_centered(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                 const char* s, uint32_t fg, uint32_t bg,
                                 bool transparent_bg);

// ---- Utilitários ------------------------------------------------
void fb_scroll_up(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t lines, uint32_t fill_color);

// ---- Cursor do mouse -------------------------------------------
void fb_draw_cursor(uint32_t x, uint32_t y);

// ---- Cache do fundo (para restaurar background sem recomputar) --
void fb_save_bg(void);     // snapshot do shadow buffer → cache
void fb_restore_bg(void);  // restaura cache → shadow buffer
