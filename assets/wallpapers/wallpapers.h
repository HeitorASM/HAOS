// wallpapers.h — gerado automaticamente por img2wallpaper.py
// Inclua este arquivo em gui/screens/desktop.c ou config.c
#pragma once

#include "wallpaper_wallpaper_op_2.h"

// Número total de wallpapers disponíveis
#define WALLPAPER_COUNT 1

// Resolução para a qual os wallpapers foram convertidos
#define WALLPAPER_W 1024
#define WALLPAPER_H 768

// Tabela de ponteiros: wallpaper_table[i] dá acesso ao wallpaper i
// Use extern + cast para incluir em translation units sem redefinição:
//   #include "assets/wallpapers/wallpapers.h"
//   const unsigned int *wp = wallpaper_table[config_wallpaper_index];

#ifdef WALLPAPER_TABLE_IMPL
const unsigned int *wallpaper_table[WALLPAPER_COUNT] = {
    wallpaper_wallpaper_op_2,
};
#else
extern const unsigned int *wallpaper_table[WALLPAPER_COUNT];
#endif

// Enum para usar no config_get_wallpaper() / config_set_wallpaper()
typedef enum {
    WALLPAPER_WALLPAPER_OP_2 = 0,
    WALLPAPER_NONE = 1,  // cor sólida / gradiente padrão
} WallpaperID;
