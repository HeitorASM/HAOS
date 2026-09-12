// gui/wallpaper.c — Sistema de wallpaper do HAOS
//
// Este arquivo funciona em dois modos:
//
//  MODO A — Sem wallpapers convertidos (padrão):
//    wallpaper_table_size == 0, wallpaper_draw() usa gradiente padrão.
//    Nenhum arquivo externo é necessário.
//
//  MODO B — Com wallpapers:
//    Depois de rodar tools/img2wallpaper.py e adicionar os .c em
//    WALLPAPER_SRCS no Makefile, defina HAOS_HAS_WALLPAPERS no Makefile:
//      CFLAGS += -DHAOS_HAS_WALLPAPERS
//    e inclua assets/wallpapers/wallpapers.h (com WALLPAPER_TABLE_IMPL).

#include "wallpaper.h"
#include "../drivers/fb.h"
#include "../kernel/types.h"
#include "../kernel/memory.h"
#include "../fs/vfs.h"

// ── Modo B: wallpapers presentes ──────────────────────────────────────────
#ifdef HAOS_HAS_WALLPAPERS
  #define WALLPAPER_TABLE_IMPL
  #include "../assets/wallpapers/wallpapers.h"
  // wallpaper_table[], WALLPAPER_COUNT, WALLPAPER_W/H definidos pelo header
#else
  // ── Modo A: sem wallpapers — tudo zerado, sem referências externas ────
  #define WALLPAPER_COUNT 0
  #define WALLPAPER_W     1024
  #define WALLPAPER_H     768
  static const unsigned int *wallpaper_table[1] = { (void*)0 };
#endif

// ── Nomes legíveis (edite após converter suas imagens) ────────────────────
// A ordem deve corresponder ao enum WallpaperID gerado em wallpapers.h.
// Exemplo: "Nebulosa", "Montanhas", "Cidade"
static const char* wallpaper_names[] = {
    // Adicione os nomes aqui conforme converte imagens
};
#define WALLPAPER_NAMES_COUNT \
    ((int)(sizeof(wallpaper_names) / sizeof(wallpaper_names[0])))

// ── Estado interno ────────────────────────────────────────────────────────
static int           current_index = -1;
static WallpaperMode current_mode  = WALLPAPER_MODE_FILL;

static VfsNode* wallpaper_preferences_file(void) {
    VfsNode* file = vfs_resolve(vfs_root(), "/etc/haos.wallpaper");
    if (file) return file;

    VfsNode* etc = vfs_resolve(vfs_root(), "/etc");
    return etc ? vfs_touch(etc, "haos.wallpaper") : (VfsNode*)0;
}

static int parse_preference_number(const char* text, int fallback) {
    int value = 0;
    bool has_digit = false;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        has_digit = true;
        text++;
    }
    return has_digit ? value : fallback;
}

static void wallpaper_save_preferences(void) {
    VfsNode* file = wallpaper_preferences_file();
    if (!file) return;

    char index[16];
    char mode[16];
    char contents[40];
    kitoa(current_index, index);
    kitoa((int)current_mode, mode);
    kstrcpy(contents, index);
    kstrcat(contents, "\n");
    kstrcat(contents, mode);
    kstrcat(contents, "\n");
    vfs_write(file, contents);
}

// ── API ───────────────────────────────────────────────────────────────────

void wallpaper_init(void) {
    // Se há wallpapers compilados, usa o primeiro como padrão automaticamente
#ifdef HAOS_HAS_WALLPAPERS
    current_index = (WALLPAPER_COUNT > 0) ? 0 : -1;
#else
    current_index = -1;
#endif
    current_mode  = WALLPAPER_MODE_FILL;

    VfsNode* file = vfs_resolve(vfs_root(), "/etc/haos.wallpaper");
    if (!file || file->type != VFS_FILE || file->size == 0) return;

    const char* text = file->data;
    current_index = parse_preference_number(text, current_index);
    while (*text && *text != '\n') text++;
    if (*text == '\n') {
        int mode = parse_preference_number(text + 1, WALLPAPER_MODE_FILL);
        if (mode >= WALLPAPER_MODE_FILL && mode <= WALLPAPER_MODE_TILE)
            current_mode = (WallpaperMode)mode;
    }

    if (current_index < -1 || current_index >= WALLPAPER_COUNT)
        current_index = -1;
}

void wallpaper_set(int index) {
    if (index < -1 || index >= WALLPAPER_COUNT)
        current_index = -1;
    else
        current_index = index;
    wallpaper_save_preferences();
}

int wallpaper_get(void) { return current_index; }

void wallpaper_set_mode(WallpaperMode mode) {
    if (mode < WALLPAPER_MODE_FILL || mode > WALLPAPER_MODE_TILE) return;
    current_mode = mode;
    wallpaper_save_preferences();
}
WallpaperMode wallpaper_get_mode(void)      { return current_mode; }
int  wallpaper_count(void)                  { return WALLPAPER_COUNT; }

const char* wallpaper_name(int index) {
    if (index < 0 || index >= WALLPAPER_COUNT) return "Padrao (gradiente)";
    if (index < WALLPAPER_NAMES_COUNT)         return wallpaper_names[index];
    return "Wallpaper";
}

// ── Desenho ───────────────────────────────────────────────────────────────

static void wp_draw_default(uint32_t sw, uint32_t sh_d) {
    fb_fill_gradient_v(0, 0, sw, sh_d, 0x0B0E1A, 0x0B1020);
    for (uint32_t gy = 0; gy < sh_d; gy += 32)
        for (uint32_t gx = 0; gx < sw; gx += 32)
            fb_put_pixel(gx, gy, 0x0F1828);
}

static void wp_draw_fill(const unsigned int* px,
                         uint32_t wp_w, uint32_t wp_h,
                         uint32_t sw,   uint32_t sh_d) {
    for (uint32_t y = 0; y < sh_d; y++) {
        uint32_t sy = y * wp_h / sh_d;
        for (uint32_t x = 0; x < sw; x++) {
            uint32_t sx = x * wp_w / sw;
            fb_put_pixel(x, y, px[sy * wp_w + sx]);
        }
    }
}

static void wp_draw_center(const unsigned int* px,
                            uint32_t wp_w, uint32_t wp_h,
                            uint32_t sw,   uint32_t sh_d) {
    fb_fill_rect(0, 0, sw, sh_d, 0x000000);
    int32_t ox = (int32_t)(sw  - wp_w) / 2;
    int32_t oy = (int32_t)(sh_d - wp_h) / 2;
    for (uint32_t y = 0; y < wp_h; y++) {
        int32_t dy = oy + (int32_t)y;
        if (dy < 0 || (uint32_t)dy >= sh_d) continue;
        for (uint32_t x = 0; x < wp_w; x++) {
            int32_t dx = ox + (int32_t)x;
            if (dx < 0 || (uint32_t)dx >= sw) continue;
            fb_put_pixel((uint32_t)dx, (uint32_t)dy, px[y * wp_w + x]);
        }
    }
}

static void wp_draw_tile(const unsigned int* px,
                          uint32_t wp_w, uint32_t wp_h,
                          uint32_t sw,   uint32_t sh_d) {
    for (uint32_t y = 0; y < sh_d; y++)
        for (uint32_t x = 0; x < sw; x++)
            fb_put_pixel(x, y, px[(y % wp_h) * wp_w + (x % wp_w)]);
}

void wallpaper_draw(uint32_t sw, uint32_t sh) {
#define TASKBAR_H_WP 32
    uint32_t sh_d = (sh > TASKBAR_H_WP) ? (sh - TASKBAR_H_WP) : sh;

    if (current_index < 0 || current_index >= WALLPAPER_COUNT
        || wallpaper_table[current_index] == (void*)0) {
        wp_draw_default(sw, sh_d);
        return;
    }

    const unsigned int* px = wallpaper_table[current_index];
    uint32_t wp_w = WALLPAPER_W, wp_h = WALLPAPER_H;

    switch (current_mode) {
        case WALLPAPER_MODE_FILL:   wp_draw_fill  (px, wp_w, wp_h, sw, sh_d); break;
        case WALLPAPER_MODE_CENTER: wp_draw_center(px, wp_w, wp_h, sw, sh_d); break;
        case WALLPAPER_MODE_TILE:   wp_draw_tile  (px, wp_w, wp_h, sw, sh_d); break;
        default:                    wp_draw_fill  (px, wp_w, wp_h, sw, sh_d); break;
    }
}