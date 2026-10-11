#pragma once
#include "../../kernel/types.h"

// ============================================================
//  gui/core/userwin.h — janelas criadas por processos Ring 3
//
//  Um programa .hexec cria uma janela (SYS_WIN_CREATE), desenha num
//  canvas de pixels que pertence ao KERNEL (SYS_WIN_FILL/TEXT/BLIT) e
//  recebe teclado/mouse como eventos (SYS_WIN_EVENT). O WM só exibe o
//  canvas dentro da moldura da janela — o programa nunca toca na
//  memória de vídeo.
//
//  Este header é incluído tanto por C (kernel/syscall.c) quanto por
//  C++ (userwin.cpp): só tipos simples e funções extern "C".
//
//  As structs de troca com o espaço de usuário (haos_win_*) são
//  ESPELHADAS em libc/include/haos/gfx.h — mantenha o layout idêntico.
// ============================================================

// ---- Eventos (haos_win_event_t::type) ----
#define HAOS_EV_KEY          1   // a = código da tecla (ASCII ou KEY_* de kernel/keyboard.h)
#define HAOS_EV_MOUSE_DOWN   2   // a,b = x,y relativos ao canto da ÁREA CLIENTE
#define HAOS_EV_MOUSE_UP     3   // idem
#define HAOS_EV_MOUSE_MOVE   4   // a,b = x,y; c = 1 se com o botão pressionado (arrasto)
#define HAOS_EV_MOUSE_SCROLL 5   // a,b = x,y; c = quantidade (> 0 = para cima)
#define HAOS_EV_RESIZE       6   // a,b = nova largura/altura da área cliente
                                 //   (o canvas foi recriado: redesenhe tudo)
#define HAOS_EV_THEME        7   // o tema do sistema mudou: releia (gfx_theme_get) e redesenhe

// haos_win_text_t::flags
#define HAOS_TEXT_OPAQUE     1   // pinta o fundo de cada caractere com `bg`

// ---- Limites ----
#define USERWIN_MAX          16  // janelas de usuário no sistema todo
#define USERWIN_PER_PROC     4   // por processo
#define USERWIN_MAX_DIM      4096

typedef struct {
    int32_t  x, y, w, h;         // retângulo (recortado ao canvas)
    uint32_t color;              // 0xRRGGBB
} haos_win_rect_t;               // 20 bytes

typedef struct {
    int32_t  x, y;               // canto superior esquerdo do texto
    uint32_t fg, bg;             // 0xRRGGBB
    uint32_t flags;              // HAOS_TEXT_OPAQUE
    uint32_t len;                // bytes de texto (máx. 255)
    uint64_t text_ptr;           // const char* no espaço de usuário
} haos_win_text_t;               // 32 bytes

typedef struct {
    int32_t  x, y, w, h;         // destino no canvas e dimensões da origem
    uint64_t pixels_ptr;         // const uint32_t* (0xRRGGBB), w*h pixels, linhas contíguas
} haos_win_blit_t;               // 24 bytes

typedef struct {
    uint32_t type;               // HAOS_EV_*
    int32_t  a, b, c;
} haos_win_event_t;              // 16 bytes

typedef struct {
    uint32_t w, h;               // tamanho atual da área cliente (= do canvas)
} haos_win_info_t;               // 8 bytes

#ifdef __cplusplus
extern "C" {
#endif

// Todas as funções abaixo recebem `owner` (id do processo que fez a
// syscall) e só enxergam janelas criadas por ele. Nenhuma toca em
// ponteiros de usuário, EXCETO userwin_blit (`pixels` já validado por
// quem chama, em kernel/syscall.c). Retornos: ver SYS_WIN_* em
// kernel/syscall.h.

int64_t userwin_create(uint64_t owner, uint32_t w, uint32_t h, const char* title);
int     userwin_destroy(uint64_t owner, int64_t handle);
int     userwin_fill(uint64_t owner, int64_t handle, const haos_win_rect_t* r);
int     userwin_text(uint64_t owner, int64_t handle, int32_t x, int32_t y,
                     uint32_t fg, uint32_t bg, uint32_t flags,
                     const char* s, uint32_t len);
int     userwin_blit(uint64_t owner, int64_t handle, int32_t x, int32_t y,
                     int32_t w, int32_t h, const uint32_t* pixels);
int     userwin_poll(uint64_t owner, int64_t handle, haos_win_event_t* out);
int     userwin_info(uint64_t owner, int64_t handle, haos_win_info_t* out);
int     userwin_set_title(uint64_t owner, int64_t handle, const char* title, uint32_t len);

// Chamada pela task da GUI a cada frame (run_desktop): fecha as janelas
// cujo processo morreu ou pediu SYS_WIN_DESTROY. É AQUI (e não dentro
// da syscall) que a janela é destruída — ver comentário em userwin.cpp.
void    userwin_reap(void);

#ifdef __cplusplus
}
#endif
