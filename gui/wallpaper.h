// Gerencie imagens de fundo do desktop via config
#pragma once
#include "../kernel/types.h"

// ── Modos de exibição ────────────────────────────────────────────────────
typedef enum {
    WALLPAPER_MODE_FILL    = 0,  // estica/preenche a tela toda
    WALLPAPER_MODE_CENTER  = 1,  // centraliza sem redimensionar
    WALLPAPER_MODE_TILE    = 2,  // repete lado a lado
} WallpaperMode;

// ── API pública ──────────────────────────────────────────────────────────

// Inicializa com nenhum wallpaper (usa gradiente padrão)
void wallpaper_init(void);

// Define qual wallpaper usar por índice (0..N-1) ou -1 para padrão
void wallpaper_set(int index);

// Retorna o índice atual (-1 = nenhum, usa gradiente)
int  wallpaper_get(void);

// Define o modo de exibição
void wallpaper_set_mode(WallpaperMode mode);
WallpaperMode wallpaper_get_mode(void);

// Retorna o número de wallpapers disponíveis
int  wallpaper_count(void);

// Retorna o nome legível do wallpaper i (para exibir na UI)
const char* wallpaper_name(int index);

// Desenha o wallpaper no shadow buffer (chamado por desktop.c)
// Se nenhum wallpaper estiver ativo, desenha o gradiente padrão
void wallpaper_draw(uint32_t sw, uint32_t sh);