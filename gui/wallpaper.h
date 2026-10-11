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
#ifdef __cplusplus
extern "C" {
#endif

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

// ── Imagem personalizada ─────────────────────────────────────────────────
// Define uma imagem do usuário como papel de parede. `rgb` são w*h pixels
// 0x00RRGGBB (linhas contíguas), com w <= 1024 e h <= 768. A imagem é guardada
// em /etc/haos.wallpaper.img (raw RGB) e volta sozinha no próximo boot. No
// modo "preencher" ela é escalada SEM distorcer (cobre a tela e corta o excesso,
// centralizado). Escolher qualquer wallpaper embutido (wallpaper_set) desliga o
// uso da imagem, mas o arquivo continua salvo para wallpaper_use_custom().
bool wallpaper_set_custom(const uint32_t* rgb, uint32_t w, uint32_t h);
bool wallpaper_is_custom(void);       // a imagem personalizada está em uso?
bool wallpaper_use_custom(void);      // volta a usá-la (false se não há imagem salva)

// Desenha o wallpaper no shadow buffer (chamado por desktop.cpp)
// Se nenhum wallpaper estiver ativo, desenha o gradiente padrão
void wallpaper_draw(uint32_t sw, uint32_t sh);

#ifdef __cplusplus
}
#endif