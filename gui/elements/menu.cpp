#include "menu.h"
#include "taskbar.h"
#include "../core/theme.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/keyboard.h"
#include "../../kernel/memory.h"

#define MENU_PAD    4     // moldura interna
#define ITEM_H      26
#define SEP_H       9
#define GUTTER      18    // espaço do marcador "marcado"
#define MIN_W       150
#define MAX_W       360
#define MAX_LEVELS  4

struct Level {
    Menu*   menu;
    int32_t x, y, w, h;
    int     hover;       // item sob o mouse/teclado (-1 = nenhum)
    int     open_item;   // item cujo submenu está aberto no nível seguinte (-1 = nenhum)
};

static Menu         g_pool[MENU_MAX_MENUS];
static int          g_pool_used = 0;
static Level        g_lv[MAX_LEVELS];
static int          g_depth = 0;           // níveis abertos; 0 = fechado
static MenuCallback g_cb = nullptr;
static void*        g_user = nullptr;

// ------------------------------------------------------------
// Montagem
// ------------------------------------------------------------
void menu_pool_reset(void) { g_pool_used = 0; }

Menu* menu_new(void) {
    if (g_pool_used >= MENU_MAX_MENUS) return nullptr;
    Menu* m = &g_pool[g_pool_used++];
    kmemset(m, 0, sizeof(*m));
    return m;
}

bool menu_add(Menu* m, const char* label, int id, uint8_t flags, const char* shortcut) {
    if (!m || m->count >= MENU_MAX_ITEMS) return false;
    MenuItem& it = m->items[m->count++];
    kmemset(&it, 0, sizeof(it));
    if (label)    kstrncpy(it.label, label, MENU_LABEL_MAX - 1);
    if (shortcut) kstrncpy(it.shortcut, shortcut, MENU_SHORTCUT_MAX - 1);
    it.id = id;
    it.flags = flags;
    it.submenu = nullptr;
    return true;
}

bool menu_add_separator(Menu* m) {
    return menu_add(m, "", 0, MENU_FLAG_SEPARATOR);
}

Menu* menu_add_submenu(Menu* m, const char* label, uint8_t flags) {
    if (!m || m->count >= MENU_MAX_ITEMS) return nullptr;
    Menu* child = menu_new();
    if (!child) return nullptr;
    menu_add(m, label, 0, flags);
    m->items[m->count - 1].submenu = child;
    return child;
}

// ------------------------------------------------------------
// Geometria
// ------------------------------------------------------------
static int32_t item_h(const MenuItem& it) {
    return (it.flags & MENU_FLAG_SEPARATOR) ? SEP_H : ITEM_H;
}

static bool selectable(const MenuItem& it) {
    return !(it.flags & (MENU_FLAG_SEPARATOR | MENU_FLAG_DISABLED));
}

static void measure(Level& L) {
    int32_t maxw = 0;
    int32_t h = MENU_PAD * 2;
    for (int i = 0; i < L.menu->count; i++) {
        const MenuItem& it = L.menu->items[i];
        h += item_h(it);
        if (it.flags & MENU_FLAG_SEPARATOR) continue;
        int32_t w = GUTTER + (int32_t)fb_text_width(it.label) + 12;
        if (it.shortcut[0]) w += 24 + (int32_t)fb_text_width(it.shortcut);
        if (it.submenu)     w += 18;
        if (w > maxw) maxw = w;
    }
    int32_t w = maxw + MENU_PAD * 2;
    if (w < MIN_W) w = MIN_W;
    if (w > MAX_W) w = MAX_W;
    L.w = w;
    L.h = h;
}

static int32_t item_top(const Level& L, int idx) {
    int32_t y = L.y + MENU_PAD;
    for (int i = 0; i < idx; i++) y += item_h(L.menu->items[i]);
    return y;
}

static bool in_level(const Level& L, int32_t mx, int32_t my) {
    return mx >= L.x && mx < L.x + L.w && my >= L.y && my < L.y + L.h;
}

static int hit_item(const Level& L, int32_t mx, int32_t my) {
    if (mx < L.x + MENU_PAD || mx >= L.x + L.w - MENU_PAD) return -1;
    int32_t y = L.y + MENU_PAD;
    for (int i = 0; i < L.menu->count; i++) {
        int32_t h = item_h(L.menu->items[i]);
        if (my >= y && my < y + h) return i;
        y += h;
    }
    return -1;
}

static int32_t usable_height(void) {
    return (int32_t)fb_height() - (int32_t)TASKBAR_H;
}

static void clamp_y(Level& L) {
    int32_t usable = usable_height();
    if (L.y + L.h > usable) L.y = usable - L.h;
    if (L.y < 0) L.y = 0;
}

static void open_submenu(int k, int idx) {
    if (k + 1 >= MAX_LEVELS) return;
    Level& P = g_lv[k];
    Menu* child = P.menu->items[idx].submenu;
    if (!child || child->count == 0) return;

    Level& C = g_lv[k + 1];
    C.menu = child;
    C.hover = -1;
    C.open_item = -1;
    measure(C);

    int32_t x = P.x + P.w - 3;                         // encosta no pai, com leve sobreposição
    if (x + C.w > (int32_t)fb_width()) x = P.x - C.w + 3;   // sem espaço à direita: abre à esquerda
    if (x < 0) x = 0;
    C.x = x;
    C.y = item_top(P, idx) - MENU_PAD;
    clamp_y(C);

    P.open_item = idx;
    g_depth = k + 2;
}

// ------------------------------------------------------------
// Estado / ciclo de vida
// ------------------------------------------------------------
void menu_popup(Menu* root, int32_t x, int32_t y, MenuCallback cb, void* user) {
    if (!root || root->count == 0) return;
    Level& L = g_lv[0];
    L.menu = root;
    L.hover = -1;
    L.open_item = -1;
    measure(L);

    int32_t sw = (int32_t)fb_width();
    if (x + L.w > sw) x = sw - L.w;
    if (x < 0) x = 0;
    L.x = x;
    L.y = y;
    clamp_y(L);

    g_cb = cb;
    g_user = user;
    g_depth = 1;
}

void menu_close(void) {
    g_depth = 0;
    g_cb = nullptr;
    g_user = nullptr;
}

bool menu_is_open(void) { return g_depth > 0; }

bool menu_contains(int32_t mx, int32_t my) {
    for (int k = 0; k < g_depth; k++)
        if (in_level(g_lv[k], mx, my)) return true;
    return false;
}

static void activate(int id) {
    MenuCallback cb = g_cb;
    void* user = g_user;
    menu_close();                      // fecha ANTES: o callback pode abrir outro menu
    if (cb) cb(id, user);
}

// ------------------------------------------------------------
// Mouse
// ------------------------------------------------------------
void menu_update(int32_t mx, int32_t my) {
    for (int k = g_depth - 1; k >= 0; k--) {
        Level& L = g_lv[k];
        if (!in_level(L, mx, my)) continue;
        int idx = hit_item(L, mx, my);
        L.hover = idx;
        if (idx >= 0 && selectable(L.menu->items[idx])) {
            if (L.open_item != idx) {          // mudou de item: fecha o submenu anterior
                g_depth = k + 1;
                L.open_item = -1;
            }
            if (L.menu->items[idx].submenu && g_depth == k + 1)
                open_submenu(k, idx);
        }
        return;
    }
}

bool menu_mouse_down(int32_t mx, int32_t my) {
    if (!g_depth) return false;
    for (int k = g_depth - 1; k >= 0; k--) {
        Level& L = g_lv[k];
        if (!in_level(L, mx, my)) continue;
        int idx = hit_item(L, mx, my);
        if (idx < 0) return true;                       // moldura: consome sem agir
        MenuItem& it = L.menu->items[idx];
        if (!selectable(it)) return true;
        if (it.submenu) {
            if (L.open_item != idx) { g_depth = k + 1; open_submenu(k, idx); }
            return true;
        }
        activate(it.id);
        return true;
    }
    menu_close();                                       // clique fora fecha e segue adiante
    return false;
}

// ------------------------------------------------------------
// Teclado
// ------------------------------------------------------------
static void move_hover(Level& L, int dir) {
    int n = L.menu->count;
    if (n == 0) return;
    int i = L.hover;
    for (int step = 0; step < n; step++) {
        if (dir > 0) i = (i < 0) ? 0 : (i + 1) % n;
        else         i = (i <= 0) ? n - 1 : i - 1;
        if (selectable(L.menu->items[i])) { L.hover = i; return; }
    }
}

static void first_hover(Level& L) {
    L.hover = -1;
    move_hover(L, +1);
}

bool menu_key(uint8_t key) {
    if (!g_depth) return false;
    if (key == 27) { menu_close(); return true; }

    int k = g_depth - 1;
    Level& L = g_lv[k];
    if (key == KEY_DOWN) { move_hover(L, +1); return true; }
    if (key == KEY_UP)   { move_hover(L, -1); return true; }
    if (key == KEY_LEFT) {
        if (g_depth > 1) { g_depth--; g_lv[g_depth - 1].open_item = -1; }
        return true;
    }
    if (key == KEY_RIGHT || key == '\n' || key == '\r') {
        if (L.hover >= 0 && selectable(L.menu->items[L.hover])) {
            MenuItem& it = L.menu->items[L.hover];
            if (it.submenu) {
                open_submenu(k, L.hover);
                if (g_depth > k + 1) first_hover(g_lv[g_depth - 1]);
            } else if (key != KEY_RIGHT) {
                activate(it.id);
            }
        }
        return true;
    }
    return true;                                        // menu aberto: engole as outras teclas
}

// ------------------------------------------------------------
// Desenho
// ------------------------------------------------------------
void menu_draw(void) {
    if (!g_depth) return;
    const Theme* t = theme_current();

    for (int k = 0; k < g_depth; k++) {
        const Level& L = g_lv[k];
        uint32_t x = (uint32_t)L.x, y = (uint32_t)L.y;
        uint32_t w = (uint32_t)L.w, h = (uint32_t)L.h;

        if (t->window_shadow) fb_draw_shadow(x + 5, y + 5, w, h);
        fb_draw_rounded_rect(x, y, w, h, t->startmenu_border, 6);
        fb_fill_rect(x + 1, y + 1, w - 2, h - 2, t->startmenu_bg_bottom);

        uint32_t iy = y + MENU_PAD;
        for (int i = 0; i < L.menu->count; i++) {
            const MenuItem& it = L.menu->items[i];
            if (it.flags & MENU_FLAG_SEPARATOR) {
                fb_fill_rect(x + MENU_PAD + 6, iy + SEP_H / 2, w - 2 * MENU_PAD - 12, 1, t->startmenu_line);
                iy += SEP_H;
                continue;
            }
            bool sel = selectable(it);
            bool hot = sel && (i == L.hover || i == L.open_item);
            uint32_t bg = hot ? t->accent_dark : t->startmenu_item_bg;
            uint32_t fg = sel ? t->startmenu_text : t->text_dim;
            uint32_t ty = iy + (uint32_t)((ITEM_H - 2 - (int)FONT_H) / 2);

            fb_fill_rect(x + MENU_PAD, iy, w - 2 * MENU_PAD, ITEM_H - 2, bg);
            if (it.flags & MENU_FLAG_CHECKED)
                fb_fill_rect(x + MENU_PAD + 7, iy + (ITEM_H - 2) / 2 - 2, 5, 5, fg);
            fb_draw_string(x + MENU_PAD + GUTTER, ty, it.label, fg, 0, true);

            uint32_t right = x + w - MENU_PAD - 8;
            if (it.submenu) {
                fb_draw_string(right - FONT_W, ty, ">", fg, 0, true);
                right -= 18;
            }
            if (it.shortcut[0]) {
                uint32_t sw = fb_text_width(it.shortcut);
                fb_draw_string(right - sw, ty, it.shortcut, t->text_dim, 0, true);
            }
            iy += ITEM_H;
        }
    }
}
