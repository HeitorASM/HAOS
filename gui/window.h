#pragma once
#include "../kernel/types.h"

#define MAX_WINDOWS  8
#define TITLE_BAR_H  30
#define BTN_SIZE     14
#define BTN_GAP      5
#define WIN_BORDER   2
#define WIN_MIN_W    200
#define WIN_MIN_H    100

typedef enum {
    WIN_TYPE_TERMINAL = 0,
    WIN_TYPE_ABOUT,
    WIN_TYPE_DIALOG,
} WinType;

typedef struct Window {
    bool     active;
    bool     focused;
    bool     minimized;
    WinType  type;
    int32_t  x, y;
    uint32_t w, h;
    char     title[64];

    // Drag state
    bool     dragging;
    int32_t  drag_ox, drag_oy;   // offset do clique relativo à janela

    // Conteúdo
    void*    content;
    void   (*draw_content)(struct Window* win);
    void   (*on_key)(struct Window* win, uint8_t c);
} Window;

void    wm_init(void);
Window* wm_create(WinType type, int32_t x, int32_t y,
                  uint32_t w, uint32_t h, const char* title);
void    wm_close(Window* win);
void    wm_draw_all(void);
void    wm_draw_window(Window* win);
void    wm_focus(Window* win);
Window* wm_get_focused(void);
void    wm_dispatch_key(uint8_t c);
int     wm_active_count(void);

// Mouse events — retorna true se o evento foi consumido por alguma janela
bool    wm_mouse_down(int32_t mx, int32_t my);
void    wm_mouse_move(int32_t mx, int32_t my);
void    wm_mouse_up(void);
