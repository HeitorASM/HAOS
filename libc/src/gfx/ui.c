// libc/src/gfx/ui.c — toolkit de widgets (veja haos/ui.h).
//
// Modelo: árvore de widgets -> a cada redesenho: measure() (tamanho preferido,
// de baixo para cima) -> arrange() (posição final, de cima para baixo) -> draw().
// Eventos entram em handle_event(); qualquer mudança só marca `dirty` e o laço
// redesenha UMA vez depois de esvaziar a fila.
#include "haos/ui.h"
#include "haos/gfx.h"
#include <stdlib.h>
#include <string.h>

extern unsigned long uptime_ms(void);          // libc (unistd.h); declarado aqui para não puxar a libc inteira

#define ITEM_H           20
#define LIST_ROWS_DEFAULT 6
#define ENTRY_MAX_DEFAULT 255
#define MAX_FOCUS        128
#define BLINK_MS         500
#define DOUBLE_CLICK_MS  400
#define CHECK_SIZE       14

enum { W_BOX, W_LABEL, W_BUTTON, W_CHECK, W_RADIO, W_ENTRY, W_LIST, W_PROGRESS, W_SLIDER, W_SEP, W_SPACER };

struct ui_widget {
    int type;
    ui_app* app;
    ui_widget *parent, *first, *last, *next;
    int x, y, w, h;                 // posição/tamanho finais (coordenadas do canvas)
    int pw, ph;                     // tamanho preferido (measure)
    int fw, fh;                     // tamanho FIXO pedido (0 = automático)
    int expand;                     // peso para dividir a sobra
    int enabled, visible, pressed;
    int vertical, spacing, padding; // só BOX
    ui_cb cb, cb_change;
    void *ctx, *ctx_change;
    char* text;                     // label/botão/check/radio/entry
    int value, vmin, vmax;          // check/radio: value = marcado; slider: valor; progress: 0..100
    int group;                      // radio
    int cursor, scroll, cap;        // entry (cap = máx. de caracteres)
    char** items;                   // list
    int n_items, items_cap, sel, top, want_rows;
    unsigned long last_click;       // list: duplo clique
};

struct ui_app {
    char title[64];
    int req_w, req_h;
    int win;
    cv_t cv;
    unsigned cw, ch;
    ui_theme_t th;
    ui_widget *root, *focus, *hover, *capture;
    int quit, dirty, blink;
    ui_key_cb key_cb; void* key_ctx;
    ui_timer_cb timer_cb; void* timer_ctx; unsigned timer_ms; unsigned long timer_next;
};

// ------------------------------------------------------------------
//  utilidades
// ------------------------------------------------------------------
static char* dupstr(const char* s) {
    size_t n = s ? strlen(s) : 0;
    char* d = (char*)malloc(n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void text_dims(const char* s, int* w, int* h) {       // vários '\n' = várias linhas
    int lines = 1, cur = 0, mx = 0;
    for (const char* p = s ? s : ""; *p; p++) {
        if (*p == '\n') { lines++; if (cur > mx) mx = cur; cur = 0; }
        else cur++;
    }
    if (cur > mx) mx = cur;
    *w = mx * CV_FONT_W;
    *h = lines * CV_FONT_H;
}

static const uint32_t* T(ui_widget* w) { return w->app->th.c; }

static void mark(ui_widget* w) { if (w && w->app) w->app->dirty = 1; }

static ui_widget* new_widget(ui_widget* parent, int type) {
    if (!parent || parent->type != W_BOX) return NULL;
    ui_widget* w = (ui_widget*)malloc(sizeof(*w));
    if (!w) return NULL;
    memset(w, 0, sizeof(*w));
    w->type = type;
    w->app = parent->app;
    w->parent = parent;
    w->enabled = 1;
    w->visible = 1;
    w->sel = -1;
    if (parent->last) parent->last->next = w; else parent->first = w;
    parent->last = w;
    parent->app->dirty = 1;
    return w;
}

static void free_tree(ui_widget* w) {
    if (!w) return;
    for (ui_widget* c = w->first; c;) { ui_widget* n = c->next; free_tree(c); c = n; }
    if (w->text) free(w->text);
    for (int i = 0; i < w->n_items; i++) free(w->items[i]);
    if (w->items) free(w->items);
    free(w);
}

static int is_focusable(ui_widget* w) {
    if (!w->visible || !w->enabled) return 0;
    switch (w->type) {
        case W_BUTTON: case W_CHECK: case W_RADIO: case W_ENTRY: case W_LIST: case W_SLIDER: return 1;
        default: return 0;
    }
}

// Visível de verdade? (nenhum ancestral escondido)
static int shown(ui_widget* w) {
    for (; w; w = w->parent) if (!w->visible) return 0;
    return 1;
}

static void collect_focusables(ui_widget* w, ui_widget** arr, int* n) {
    if (!w->visible) return;
    if (is_focusable(w) && *n < MAX_FOCUS) arr[(*n)++] = w;
    for (ui_widget* c = w->first; c; c = c->next) collect_focusables(c, arr, n);
}

static ui_widget* first_focusable(ui_app* a) {
    ui_widget* arr[MAX_FOCUS]; int n = 0;
    collect_focusables(a->root, arr, &n);
    return n ? arr[0] : NULL;
}

static void focus_step(ui_app* a, int dir) {
    ui_widget* arr[MAX_FOCUS]; int n = 0;
    collect_focusables(a->root, arr, &n);
    if (n == 0) { a->focus = NULL; return; }
    int idx = -1;
    for (int i = 0; i < n; i++) if (arr[i] == a->focus) idx = i;
    idx = idx < 0 ? 0 : (idx + dir + n) % n;
    a->focus = arr[idx];
    a->blink = 1;
    a->dirty = 1;
}

// ------------------------------------------------------------------
//  layout
// ------------------------------------------------------------------
static void measure(ui_widget* w) {
    const uint32_t* t = T(w);
    int pw = 0, ph = 0, tw, th_;
    switch (w->type) {
    case W_BOX: {
        int n = 0, cross = 0, sum = 0;
        for (ui_widget* c = w->first; c; c = c->next) {
            if (!c->visible) continue;
            measure(c);
            n++;
            if (w->vertical) { cross = imax(cross, c->pw); sum += c->ph; }
            else             { cross = imax(cross, c->ph); sum += c->pw; }
        }
        int gaps = n > 1 ? (n - 1) * w->spacing : 0;
        if (w->vertical) { pw = cross; ph = sum + gaps; } else { pw = sum + gaps; ph = cross; }
        pw += 2 * w->padding; ph += 2 * w->padding;
        break;
    }
    case W_LABEL:   text_dims(w->text, &tw, &th_); pw = tw; ph = th_; break;
    case W_BUTTON:  text_dims(w->text, &tw, &th_); pw = tw + 32; ph = imax((int)t[UI_WIDGET_HEIGHT], 24); break;
    case W_CHECK:
    case W_RADIO:   text_dims(w->text, &tw, &th_); pw = CHECK_SIZE + 8 + tw; ph = imax(th_, 20); break;
    case W_ENTRY:   pw = 160; ph = imax((int)t[UI_WIDGET_HEIGHT], 24); break;
    case W_LIST:    pw = 200; ph = (w->want_rows > 0 ? w->want_rows : LIST_ROWS_DEFAULT) * ITEM_H + 4; break;
    case W_PROGRESS: pw = 160; ph = 16; break;
    case W_SLIDER:  pw = 160; ph = 22; break;
    case W_SEP:     pw = 1; ph = 1; break;
    default:        pw = 0; ph = 0; break;                 // spacer
    }
    if (w->fw > 0) pw = w->fw;
    if (w->fh > 0) ph = w->fh;
    w->pw = pw; w->ph = ph;
}

static void arrange(ui_widget* w, int x, int y, int ww, int hh) {
    w->x = x; w->y = y; w->w = ww; w->h = hh;
    if (w->type != W_BOX) return;

    int ix = x + w->padding, iy = y + w->padding;
    int iw = imax(ww - 2 * w->padding, 0), ih = imax(hh - 2 * w->padding, 0);
    int n = 0, total = 0, weight = 0;
    for (ui_widget* c = w->first; c; c = c->next) {
        if (!c->visible) continue;
        n++;
        total += w->vertical ? c->ph : c->pw;
        weight += c->expand;
    }
    int gaps = n > 1 ? (n - 1) * w->spacing : 0;
    int avail = (w->vertical ? ih : iw) - gaps;
    int extra = avail - total;                    // > 0 sobra; < 0 falta
    int pos = w->vertical ? iy : ix;
    int given = 0, seen = 0;

    for (ui_widget* c = w->first; c; c = c->next) {
        if (!c->visible) continue;
        int main = w->vertical ? c->ph : c->pw;
        if (c->expand > 0 && weight > 0) {
            seen += c->expand;
            int share = (seen == weight) ? extra - given : (int)((long)extra * c->expand / weight);
            given += share;
            main = imax(main + share, 0);
        }
        if (w->vertical) {
            arrange(c, ix, pos, c->fw > 0 ? c->fw : iw, main);
        } else {
            arrange(c, pos, iy, main, c->fh > 0 ? c->fh : ih);
        }
        pos += main + w->spacing;
    }
}

// ------------------------------------------------------------------
//  desenho
// ------------------------------------------------------------------
static void draw_text_lines(cv_t* cv, int x, int y, const char* s, uint32_t color) {
    char line[256];
    int n = 0;
    for (const char* p = s ? s : ""; ; p++) {
        if (*p == '\n' || *p == '\0') {
            line[n] = '\0';
            cv_text(cv, x, y, line, color, CV_NO_BG);
            y += CV_FONT_H; n = 0;
            if (*p == '\0') break;
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = *p;
        }
    }
}

static void draw_focus_ring(cv_t* cv, ui_widget* w, int radius) {
    if (w->app->focus != w) return;
    uint32_t c = T(w)[UI_BUTTON_BORDER_FOCUS];
    if (radius > 1) cv_round(cv, w->x, w->y, w->w, w->h, radius, c);
    else            cv_rect(cv, w->x, w->y, w->w, w->h, c);
}

static void entry_view(ui_widget* w, int* maxc) {
    int avail = imax(w->w - 12, CV_FONT_W);
    *maxc = imax(avail / CV_FONT_W, 1);
    int len = (int)strlen(w->text);
    w->cursor = clampi(w->cursor, 0, len);
    if (w->cursor < w->scroll) w->scroll = w->cursor;
    if (w->cursor - w->scroll > *maxc - 1 && len >= *maxc) w->scroll = w->cursor - (*maxc - 1);
    if (w->scroll < 0) w->scroll = 0;
    if (w->scroll > len) w->scroll = len;
}

static void draw_widget(ui_widget* w) {
    ui_app* a = w->app;
    cv_t* cv = &a->cv;
    const uint32_t* t = a->th.c;
    int enabled = w->enabled;
    uint32_t txt = enabled ? t[UI_TEXT] : t[UI_TEXT_DIM];

    switch (w->type) {
    case W_LABEL:
        draw_text_lines(cv, w->x, w->y + (w->h > w->ph ? (w->h - w->ph) / 2 : 0), w->text, txt);
        break;

    case W_BUTTON: {
        int down = enabled && w->pressed && a->hover == w;
        uint32_t bg = !enabled ? t[UI_BUTTON_BG] : down ? t[UI_BUTTON_PRESSED] : (a->hover == w ? t[UI_BUTTON_HOVER] : t[UI_BUTTON_BG]);
        int r = t[UI_BUTTONS_ROUNDED] ? (int)t[UI_BUTTON_RADIUS] : 0;
        if (r > 1) { cv_fill_round(cv, w->x, w->y, w->w, w->h, r, bg); cv_round(cv, w->x, w->y, w->w, w->h, r, t[UI_BUTTON_BORDER]); }
        else       { cv_fill(cv, w->x, w->y, w->w, w->h, bg);          cv_rect(cv, w->x, w->y, w->w, w->h, t[UI_BUTTON_BORDER]); }
        draw_focus_ring(cv, w, r);
        int tw, th_; text_dims(w->text, &tw, &th_);
        draw_text_lines(cv, w->x + (w->w - tw) / 2, w->y + (w->h - th_) / 2 + (down ? 1 : 0),
                        w->text, enabled ? t[UI_BUTTON_FG] : t[UI_TEXT_DIM]);
        break;
    }

    case W_CHECK:
    case W_RADIO: {
        int by = w->y + (w->h - CHECK_SIZE) / 2;
        uint32_t border = a->focus == w ? t[UI_BUTTON_BORDER_FOCUS] : t[UI_CHECK_BORDER];
        if (w->type == W_CHECK) {
            cv_fill(cv, w->x, by, CHECK_SIZE, CHECK_SIZE, t[UI_CHECK_BG]);
            cv_rect(cv, w->x, by, CHECK_SIZE, CHECK_SIZE, border);
            if (w->value) {                                    // "✓" com duas linhas de 2 px
                cv_line(cv, w->x + 3, by + 7, w->x + 6, by + 10, t[UI_CHECK_MARK]);
                cv_line(cv, w->x + 3, by + 8, w->x + 6, by + 11, t[UI_CHECK_MARK]);
                cv_line(cv, w->x + 6, by + 10, w->x + 11, by + 4, t[UI_CHECK_MARK]);
                cv_line(cv, w->x + 6, by + 11, w->x + 11, by + 5, t[UI_CHECK_MARK]);
            }
        } else {
            cv_fill_round(cv, w->x, by, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE / 2, t[UI_CHECK_BG]);
            cv_round(cv, w->x, by, CHECK_SIZE, CHECK_SIZE, CHECK_SIZE / 2, border);
            if (w->value) cv_fill_round(cv, w->x + 4, by + 4, 6, 6, 3, t[UI_CHECK_MARK]);
        }
        int tw, th_; text_dims(w->text, &tw, &th_);
        draw_text_lines(cv, w->x + CHECK_SIZE + 8, w->y + (w->h - th_) / 2, w->text, txt);
        break;
    }

    case W_ENTRY: {
        int maxc; entry_view(w, &maxc);
        cv_fill(cv, w->x, w->y, w->w, w->h, t[UI_FIELD_BG]);
        cv_rect(cv, w->x, w->y, w->w, w->h, a->focus == w ? t[UI_FIELD_BORDER_FOCUS] : t[UI_FIELD_BORDER]);
        char vis[ENTRY_MAX_DEFAULT + 2];
        int len = (int)strlen(w->text), n = imin(len - w->scroll, imin(maxc, (int)sizeof(vis) - 1));
        if (n < 0) n = 0;
        memcpy(vis, w->text + w->scroll, (size_t)n);
        vis[n] = '\0';
        int ty = w->y + (w->h - CV_FONT_H) / 2;
        cv_text(cv, w->x + 6, ty, vis, enabled ? t[UI_FIELD_FG] : t[UI_TEXT_DIM], CV_NO_BG);
        if (a->focus == w && a->blink && enabled)
            cv_vline(cv, w->x + 6 + (w->cursor - w->scroll) * CV_FONT_W, ty, CV_FONT_H, t[UI_FIELD_CURSOR]);
        break;
    }

    case W_LIST: {
        cv_fill(cv, w->x, w->y, w->w, w->h, t[UI_LIST_BG]);
        int rows = imax((w->h - 4) / ITEM_H, 1);
        int clip_x = w->x + 1, clip_y = w->y + 1, clip_w = w->w - 2, clip_h = w->h - 2;
        cv_set_clip(cv, clip_x, clip_y, clip_w, clip_h);
        for (int r = 0; r < rows; r++) {
            int i = w->top + r;
            if (i >= w->n_items) break;
            int iy = w->y + 2 + r * ITEM_H;
            uint32_t bg = (i == w->sel) ? t[UI_LIST_SELECTED] : (i & 1 ? t[UI_LIST_ITEM] : t[UI_LIST_BG]);
            cv_fill(cv, w->x + 1, iy, w->w - 2, ITEM_H, bg);
            cv_text(cv, w->x + 6, iy + (ITEM_H - CV_FONT_H) / 2, w->items[i], t[UI_LIST_FG], CV_NO_BG);
        }
        if (w->n_items > rows) {                               // barra de rolagem fina
            int track = w->h - 4;
            int thumb = imax(track * rows / w->n_items, 10);
            int maxtop = w->n_items - rows;
            int ty = w->y + 2 + (maxtop > 0 ? (track - thumb) * w->top / maxtop : 0);
            cv_fill(cv, w->x + w->w - 5, ty, 3, thumb, t[UI_PANEL_BORDER]);
        }
        cv_reset_clip(cv);
        cv_rect(cv, w->x, w->y, w->w, w->h, a->focus == w ? t[UI_BUTTON_BORDER_FOCUS] : t[UI_PANEL_BORDER]);
        break;
    }

    case W_PROGRESS: {
        cv_fill(cv, w->x, w->y, w->w, w->h, t[UI_PANEL_BG]);
        int fill = (w->w - 2) * clampi(w->value, 0, 100) / 100;
        cv_fill(cv, w->x + 1, w->y + 1, fill, w->h - 2, t[UI_ACCENT]);
        cv_rect(cv, w->x, w->y, w->w, w->h, t[UI_PANEL_BORDER]);
        break;
    }

    case W_SLIDER: {
        int x0 = w->x + 7, span = imax(w->w - 14, 1), cy = w->y + w->h / 2;
        int range = w->vmax - w->vmin;
        int kx = x0 + (range > 0 ? (int)((long)(w->value - w->vmin) * span / range) : 0);
        cv_fill(cv, x0, cy - 2, span, 4, t[UI_PANEL_BG]);
        cv_rect(cv, x0, cy - 2, span, 4, t[UI_PANEL_BORDER]);
        cv_fill(cv, x0, cy - 1, kx - x0, 2, enabled ? t[UI_ACCENT] : t[UI_TEXT_DIM]);
        cv_fill(cv, kx - 6, w->y + 2, 12, w->h - 4, a->hover == w || w->pressed ? t[UI_BUTTON_HOVER] : t[UI_BUTTON_BG]);
        cv_rect(cv, kx - 6, w->y + 2, 12, w->h - 4, a->focus == w ? t[UI_BUTTON_BORDER_FOCUS] : t[UI_BUTTON_BORDER]);
        break;
    }

    case W_SEP:
        if (w->w >= w->h) cv_hline(cv, w->x, w->y + w->h / 2, w->w, t[UI_PANEL_BORDER]);
        else              cv_vline(cv, w->x + w->w / 2, w->y, w->h, t[UI_PANEL_BORDER]);
        break;

    default: break;
    }
}

static void draw_tree(ui_widget* w) {
    if (!w->visible) return;
    if (w->type != W_BOX) draw_widget(w);
    for (ui_widget* c = w->first; c; c = c->next) draw_tree(c);
}

static void render(ui_app* a) {
    cv_reset_clip(&a->cv);
    cv_fill(&a->cv, 0, 0, a->cv.w, a->cv.h, a->th.c[UI_WINDOW_BG]);
    measure(a->root);
    arrange(a->root, 0, 0, (int)a->cw, (int)a->ch);
    draw_tree(a->root);
    cv_present(&a->cv, a->win);
}

// ------------------------------------------------------------------
//  hit-test e edição
// ------------------------------------------------------------------
static ui_widget* hit(ui_widget* w, int x, int y) {
    if (!w->visible) return NULL;
    if (x < w->x || x >= w->x + w->w || y < w->y || y >= w->y + w->h) return NULL;
    ui_widget* found = NULL;
    for (ui_widget* c = w->first; c; c = c->next) {
        ui_widget* r = hit(c, x, y);
        if (r) found = r;
    }
    if (found) return found;
    return w->type == W_BOX ? NULL : w;
}

static void fire(ui_cb cb, ui_widget* w, void* ctx) { if (cb) cb(w, ctx); }

static void entry_changed(ui_widget* w) { mark(w); fire(w->cb_change, w, w->ctx_change); }

static int entry_insert(ui_widget* w, const char* s, int n) {
    int len = (int)strlen(w->text);
    if (n <= 0 || len >= w->cap) return 0;
    if (len + n > w->cap) n = w->cap - len;
    memmove(w->text + w->cursor + n, w->text + w->cursor, (size_t)(len - w->cursor + 1));
    memcpy(w->text + w->cursor, s, (size_t)n);
    w->cursor += n;
    return n;
}

static void entry_delete(ui_widget* w, int from, int count) {
    int len = (int)strlen(w->text);
    if (from < 0 || from >= len || count <= 0) return;
    if (from + count > len) count = len - from;
    memmove(w->text + from, w->text + from + count, (size_t)(len - from - count + 1));
}

static void select_radio(ui_widget* w) {
    if (w->parent)
        for (ui_widget* s = w->parent->first; s; s = s->next)
            if (s->type == W_RADIO && s->group == w->group) s->value = (s == w);
    mark(w);
}

static void slider_from_x(ui_widget* w, int mx) {
    int x0 = w->x + 7, span = imax(w->w - 14, 1), range = w->vmax - w->vmin;
    int v = w->vmin + (int)((long)clampi(mx - x0, 0, span) * range / span);
    if (v != w->value) { w->value = v; mark(w); fire(w->cb, w, w->ctx); }
}

static void list_ensure_visible(ui_widget* w) {
    int rows = imax((w->h - 4) / ITEM_H, 1);
    if (w->sel < w->top) w->top = w->sel;
    if (w->sel >= w->top + rows) w->top = w->sel - rows + 1;
    int maxtop = imax(w->n_items - rows, 0);
    w->top = clampi(w->top, 0, maxtop);
}

static void list_select(ui_widget* w, int idx) {
    idx = clampi(idx, w->n_items ? 0 : -1, w->n_items - 1);
    if (idx == w->sel) return;
    w->sel = idx;
    list_ensure_visible(w);
    mark(w);
    fire(w->cb_change, w, w->ctx_change);
}

static void activate(ui_widget* w) {            // clique / Enter / Espaço no widget
    if (!w->enabled) return;
    switch (w->type) {
        case W_BUTTON: fire(w->cb, w, w->ctx); break;
        case W_CHECK:  w->value = !w->value; mark(w); fire(w->cb, w, w->ctx); break;
        case W_RADIO:  if (!w->value) { select_radio(w); fire(w->cb, w, w->ctx); } break;
        default: break;
    }
}

// ------------------------------------------------------------------
//  eventos
// ------------------------------------------------------------------
static int key_on_widget(ui_widget* w, int k) {              // 1 = tratou
    ui_app* a = w->app;
    switch (w->type) {
    case W_BUTTON:
        if (k == '\n' || k == '\r' || k == ' ') { activate(w); return 1; }
        return 0;
    case W_CHECK:
        if (k == ' ' || k == '\n' || k == '\r') { activate(w); return 1; }
        return 0;
    case W_RADIO: {
        if (k == ' ' || k == '\n' || k == '\r') { activate(w); return 1; }
        int dir = (k == HAOS_KEY_DOWN || k == HAOS_KEY_RIGHT) ? 1 : (k == HAOS_KEY_UP || k == HAOS_KEY_LEFT) ? -1 : 0;
        if (!dir || !w->parent) return 0;
        ui_widget* arr[MAX_FOCUS]; int n = 0, idx = -1;
        for (ui_widget* s = w->parent->first; s && n < MAX_FOCUS; s = s->next)
            if (s->type == W_RADIO && s->group == w->group && s->visible && s->enabled) { if (s == w) idx = n; arr[n++] = s; }
        if (idx < 0 || n < 2) return 1;
        ui_widget* nx = arr[(idx + dir + n) % n];
        a->focus = nx; select_radio(nx); fire(nx->cb, nx, nx->ctx);
        return 1;
    }
    case W_ENTRY: {
        int len = (int)strlen(w->text);
        if (k == '\n' || k == '\r') { fire(w->cb, w, w->ctx); return 1; }
        if (k >= 32 && k < 127) { char c = (char)k; if (entry_insert(w, &c, 1)) entry_changed(w); return 1; }
        if (k == '\b' || k == 0x08) { if (w->cursor > 0) { entry_delete(w, w->cursor - 1, 1); w->cursor--; entry_changed(w); } return 1; }
        if (k == HAOS_KEY_DELETE)   { if (w->cursor < len) { entry_delete(w, w->cursor, 1); entry_changed(w); } return 1; }
        if (k == HAOS_KEY_LEFT)     { if (w->cursor > 0) w->cursor--; mark(w); return 1; }
        if (k == HAOS_KEY_RIGHT)    { if (w->cursor < len) w->cursor++; mark(w); return 1; }
        if (k == HAOS_KEY_HOME)     { w->cursor = 0; mark(w); return 1; }
        if (k == HAOS_KEY_END)      { w->cursor = len; mark(w); return 1; }
        if (k == HAOS_KEY_CTRL_C || k == HAOS_KEY_CTRL_X) {
            clip_set(w->text, (unsigned long)len);
            if (k == HAOS_KEY_CTRL_X && len) { w->text[0] = '\0'; w->cursor = 0; w->scroll = 0; entry_changed(w); }
            return 1;
        }
        if (k == HAOS_KEY_CTRL_V) {
            char buf[ENTRY_MAX_DEFAULT + 1];
            long total = clip_get(buf, sizeof(buf) - 1);
            int n = 0;
            if (total > 0) {
                int avail = (int)(total < (long)sizeof(buf) - 1 ? total : (long)sizeof(buf) - 1);
                while (n < avail && buf[n] != '\n' && buf[n] != '\r') { if ((unsigned char)buf[n] < 32) buf[n] = ' '; n++; }
            }
            if (entry_insert(w, buf, n)) entry_changed(w);
            return 1;
        }
        return 0;
    }
    case W_LIST: {
        int rows = imax((w->h - 4) / ITEM_H, 1);
        if (k == HAOS_KEY_DOWN)      { list_select(w, w->sel < 0 ? 0 : w->sel + 1); return 1; }
        if (k == HAOS_KEY_UP)        { list_select(w, w->sel < 0 ? 0 : w->sel - 1); return 1; }
        if (k == HAOS_KEY_PAGE_DOWN) { list_select(w, w->sel + rows); return 1; }
        if (k == HAOS_KEY_PAGE_UP)   { list_select(w, w->sel - rows); return 1; }
        if (k == HAOS_KEY_HOME)      { list_select(w, 0); return 1; }
        if (k == HAOS_KEY_END)       { list_select(w, w->n_items - 1); return 1; }
        if (k == '\n' || k == '\r')  { if (w->sel >= 0) fire(w->cb, w, w->ctx); return 1; }
        return 0;
    }
    case W_SLIDER: {
        int range = w->vmax - w->vmin, step = imax(range / 20, 1), v = w->value;
        if (k == HAOS_KEY_LEFT || k == HAOS_KEY_DOWN)  v -= step;
        else if (k == HAOS_KEY_RIGHT || k == HAOS_KEY_UP) v += step;
        else if (k == HAOS_KEY_HOME) v = w->vmin;
        else if (k == HAOS_KEY_END)  v = w->vmax;
        else return 0;
        v = clampi(v, w->vmin, w->vmax);
        if (v != w->value) { w->value = v; mark(w); fire(w->cb, w, w->ctx); }
        return 1;
    }
    default: return 0;
    }
}

static void handle_key(ui_app* a, int k) {
    a->blink = 1;
    a->dirty = 1;
    if (a->focus && shown(a->focus) && a->focus->enabled && key_on_widget(a->focus, k)) return;
    if (k == '\t') { focus_step(a, +1); return; }
    if (a->key_cb) a->key_cb(a, k, a->key_ctx);
}

static void handle_mouse_down(ui_app* a, int x, int y) {
    ui_widget* w = hit(a->root, x, y);
    a->hover = w;
    if (!w || !w->enabled) { if (!w) a->focus = NULL; a->dirty = 1; return; }
    if (is_focusable(w)) a->focus = w;
    a->blink = 1;
    switch (w->type) {
    case W_BUTTON: case W_CHECK: case W_RADIO:
        w->pressed = 1; a->capture = w; break;
    case W_ENTRY: {
        int maxc; entry_view(w, &maxc);
        int idx = w->scroll + (x - (w->x + 6) + CV_FONT_W / 2) / CV_FONT_W;
        w->cursor = clampi(idx, 0, (int)strlen(w->text));
        break;
    }
    case W_LIST: {
        int idx = w->top + (y - (w->y + 2)) / ITEM_H;
        if (idx >= 0 && idx < w->n_items) {
            unsigned long now = uptime_ms();
            int dbl = (idx == w->sel) && w->last_click && now - w->last_click <= DOUBLE_CLICK_MS;
            list_select(w, idx);
            w->last_click = now;
            if (dbl) fire(w->cb, w, w->ctx);
        }
        break;
    }
    case W_SLIDER:
        w->pressed = 1; a->capture = w; slider_from_x(w, x); break;
    default: break;
    }
    a->dirty = 1;
}

static void handle_mouse_up(ui_app* a, int x, int y) {
    ui_widget* c = a->capture;
    a->capture = NULL;
    if (!c) return;
    c->pressed = 0;
    a->dirty = 1;
    ui_widget* over = hit(a->root, x, y);
    a->hover = over;
    if (over == c && (c->type == W_BUTTON || c->type == W_CHECK || c->type == W_RADIO)) activate(c);
}

static void handle_event(ui_app* a, const struct haos_win_event* ev) {
    switch (ev->type) {
    case HAOS_EV_KEY:        handle_key(a, ev->a); break;
    case HAOS_EV_MOUSE_DOWN: handle_mouse_down(a, ev->a, ev->b); break;
    case HAOS_EV_MOUSE_UP:   handle_mouse_up(a, ev->a, ev->b); break;
    case HAOS_EV_MOUSE_MOVE: {
        ui_widget* h = hit(a->root, ev->a, ev->b);
        if (h && !h->enabled) h = NULL;
        if (h != a->hover) { a->hover = h; a->dirty = 1; }
        if (a->capture && a->capture->type == W_SLIDER && ev->c == 1) slider_from_x(a->capture, ev->a);
        break;
    }
    case HAOS_EV_MOUSE_SCROLL: {
        ui_widget* h = hit(a->root, ev->a, ev->b);
        if (!h || h->type != W_LIST) h = (a->focus && a->focus->type == W_LIST) ? a->focus : NULL;
        if (h) {
            int rows = imax((h->h - 4) / ITEM_H, 1);
            int maxtop = imax(h->n_items - rows, 0);
            h->top = clampi(h->top - ev->c * 3, 0, maxtop);
            a->dirty = 1;
        }
        break;
    }
    case HAOS_EV_RESIZE:
        if (ev->a > 0 && ev->b > 0 && cv_resize(&a->cv, ev->a, ev->b) == 0) {
            a->cw = (unsigned)ev->a; a->ch = (unsigned)ev->b;
        }
        a->dirty = 1;
        break;
    case HAOS_EV_THEME:
        ui_theme_load(&a->th);
        a->dirty = 1;
        break;
    default: break;
    }
}

// ------------------------------------------------------------------
//  aplicativo
// ------------------------------------------------------------------
ui_app* ui_app_new(const char* title, int w, int h) {
    ui_app* a = (ui_app*)malloc(sizeof(*a));
    if (!a) return NULL;
    memset(a, 0, sizeof(*a));
    a->win = -1;
    a->req_w = w; a->req_h = h;
    if (title) { strncpy(a->title, title, sizeof(a->title) - 1); a->title[sizeof(a->title) - 1] = '\0'; }
    ui_theme_load(&a->th);
    ui_widget* r = (ui_widget*)malloc(sizeof(*r));
    if (!r) { free(a); return NULL; }
    memset(r, 0, sizeof(*r));
    r->type = W_BOX; r->app = a; r->enabled = 1; r->visible = 1; r->vertical = 1; r->sel = -1;
    r->spacing = (int)a->th.c[UI_SPACING_MD];
    r->padding = (int)a->th.c[UI_SPACING_LG];
    a->root = r;
    a->dirty = 1;
    return a;
}

void ui_app_free(ui_app* a) {
    if (!a) return;
    if (a->win >= 0) win_destroy(a->win);
    cv_free(&a->cv);
    free_tree(a->root);
    free(a);
}

ui_widget* ui_root(ui_app* a) { return a ? a->root : NULL; }
int  ui_window(ui_app* a)     { return a ? a->win : -1; }
void ui_quit(ui_app* a)       { if (a) a->quit = 1; }
void ui_redraw(ui_app* a)     { if (a) a->dirty = 1; }

void ui_set_title(ui_app* a, const char* title) {
    if (!a || !title) return;
    strncpy(a->title, title, sizeof(a->title) - 1);
    a->title[sizeof(a->title) - 1] = '\0';
    if (a->win >= 0) win_set_title(a->win, a->title);
}

void ui_set_key_handler(ui_app* a, ui_key_cb cb, void* ctx) { if (a) { a->key_cb = cb; a->key_ctx = ctx; } }

void ui_set_timer(ui_app* a, unsigned ms, ui_timer_cb cb, void* ctx) {
    if (!a) return;
    a->timer_ms = ms; a->timer_cb = cb; a->timer_ctx = ctx;
    a->timer_next = ms ? uptime_ms() + ms : 0;
}

static int ensure_window(ui_app* a) {
    if (a->win >= 0) return 0;
    int w = a->req_w, h = a->req_h;
    if (w <= 0 || h <= 0) {                               // auto: tamanho do conteúdo
        measure(a->root);
        w = imax(a->root->pw, 160);
        h = imax(a->root->ph, 48);
    }
    a->win = win_create((unsigned)w, (unsigned)h, a->title);
    if (a->win < 0) return -1;
    unsigned cw = 0, ch = 0;
    win_size(a->win, &cw, &ch);
    if (cv_init(&a->cv, (int)cw, (int)ch) != 0) return -1;
    a->cw = cw; a->ch = ch;
    return 0;
}

static unsigned next_timeout(ui_app* a) {
    unsigned tm = WIN_WAIT_FOREVER;
    if (a->focus && a->focus->type == W_ENTRY) tm = BLINK_MS;
    if (a->timer_ms) {
        unsigned long now = uptime_ms();
        unsigned rem = a->timer_next > now ? (unsigned)(a->timer_next - now) : 0;
        if (rem < tm) tm = rem;
    }
    return tm;
}

static void on_timeout(ui_app* a) {
    if (a->timer_ms && uptime_ms() >= a->timer_next) {
        a->timer_next = uptime_ms() + a->timer_ms;
        if (a->timer_cb) a->timer_cb(a, a->timer_ctx);
    }
    if (a->focus && a->focus->type == W_ENTRY) { a->blink = !a->blink; a->dirty = 1; }
}

int ui_run(ui_app* a) {
    if (!a) return -1;
    if (ensure_window(a) != 0) return -1;
    a->quit = 0;
    if (!a->focus || !shown(a->focus)) a->focus = first_focusable(a);
    a->blink = 1;
    if (a->timer_ms) a->timer_next = uptime_ms() + a->timer_ms;
    render(a);
    a->dirty = 0;

    struct haos_win_event ev;
    while (!a->quit) {
        int r = win_event(a->win, &ev, next_timeout(a));
        if (r < 0) break;                                       // janela fechada
        if (r == 0) {
            on_timeout(a);
        } else {
            handle_event(a, &ev);
            while (!a->quit && (r = win_event(a->win, &ev, 0)) == 1) handle_event(a, &ev);   // junta a fila: 1 redesenho
            if (r < 0) break;
        }
        if (a->dirty && !a->quit) { render(a); a->dirty = 0; }
    }
    return 0;
}

// ------------------------------------------------------------------
//  API de widgets
// ------------------------------------------------------------------
ui_widget* ui_vbox(ui_widget* parent, int spacing) {
    ui_widget* w = new_widget(parent, W_BOX);
    if (w) { w->vertical = 1; w->spacing = spacing; }
    return w;
}
ui_widget* ui_hbox(ui_widget* parent, int spacing) {
    ui_widget* w = new_widget(parent, W_BOX);
    if (w) { w->vertical = 0; w->spacing = spacing; }
    return w;
}
void ui_set_padding(ui_widget* box, int px) { if (box && box->type == W_BOX) { box->padding = px < 0 ? 0 : px; mark(box); } }

ui_widget* ui_label(ui_widget* parent, const char* text) {
    ui_widget* w = new_widget(parent, W_LABEL);
    if (w) w->text = dupstr(text);
    return w;
}
void ui_label_set_text(ui_widget* w, const char* text) {
    if (!w || w->type != W_LABEL) return;
    char* n = dupstr(text);
    if (!n) return;
    free(w->text); w->text = n; mark(w);
}

ui_widget* ui_button(ui_widget* parent, const char* text, ui_cb on_click, void* ctx) {
    ui_widget* w = new_widget(parent, W_BUTTON);
    if (w) { w->text = dupstr(text); w->cb = on_click; w->ctx = ctx; }
    return w;
}

ui_widget* ui_check(ui_widget* parent, const char* text, int checked, ui_cb on_change, void* ctx) {
    ui_widget* w = new_widget(parent, W_CHECK);
    if (w) { w->text = dupstr(text); w->value = checked ? 1 : 0; w->cb = on_change; w->ctx = ctx; }
    return w;
}
int  ui_check_get(ui_widget* w) { return w && w->type == W_CHECK ? w->value : 0; }
void ui_check_set(ui_widget* w, int checked) { if (w && w->type == W_CHECK) { w->value = checked ? 1 : 0; mark(w); } }

ui_widget* ui_radio(ui_widget* parent, const char* text, int group, ui_cb on_select, void* ctx) {
    ui_widget* w = new_widget(parent, W_RADIO);
    if (w) { w->text = dupstr(text); w->group = group; w->cb = on_select; w->ctx = ctx; }
    return w;
}
int  ui_radio_get(ui_widget* w) { return w && w->type == W_RADIO ? w->value : 0; }
void ui_radio_set(ui_widget* w) { if (w && w->type == W_RADIO) select_radio(w); }

ui_widget* ui_entry(ui_widget* parent, const char* text, ui_cb on_activate, void* ctx) {
    ui_widget* w = new_widget(parent, W_ENTRY);
    if (!w) return NULL;
    w->cap = ENTRY_MAX_DEFAULT;
    w->text = (char*)malloc((size_t)w->cap + 1);
    if (!w->text) { w->visible = 0; return NULL; }
    w->text[0] = '\0';
    w->cb = on_activate; w->ctx = ctx;
    ui_entry_set_text(w, text);
    return w;
}
const char* ui_entry_text(ui_widget* w) { return w && w->type == W_ENTRY ? w->text : ""; }
void ui_entry_set_text(ui_widget* w, const char* text) {
    if (!w || w->type != W_ENTRY) return;
    size_t n = text ? strlen(text) : 0;
    if (n > (size_t)w->cap) n = (size_t)w->cap;
    if (n) memcpy(w->text, text, n);
    w->text[n] = '\0';
    w->cursor = (int)n; w->scroll = 0;
    mark(w);
}
void ui_entry_set_max(ui_widget* w, int max_chars) {
    if (!w || w->type != W_ENTRY || max_chars < 1 || max_chars > ENTRY_MAX_DEFAULT) return;
    w->cap = max_chars;
    if ((int)strlen(w->text) > max_chars) w->text[max_chars] = '\0';
    if (w->cursor > max_chars) w->cursor = max_chars;
    mark(w);
}

ui_widget* ui_list(ui_widget* parent, ui_cb on_activate, void* ctx) {
    ui_widget* w = new_widget(parent, W_LIST);
    if (w) { w->cb = on_activate; w->ctx = ctx; w->want_rows = LIST_ROWS_DEFAULT; }
    return w;
}
int ui_list_add(ui_widget* w, const char* text) {
    if (!w || w->type != W_LIST) return -1;
    if (w->n_items == w->items_cap) {
        int nc = w->items_cap ? w->items_cap * 2 : 16;
        char** ni = (char**)realloc(w->items, (size_t)nc * sizeof(char*));
        if (!ni) return -1;
        w->items = ni; w->items_cap = nc;
    }
    char* d = dupstr(text);
    if (!d) return -1;
    w->items[w->n_items] = d;
    mark(w);
    return w->n_items++;
}
void ui_list_clear(ui_widget* w) {
    if (!w || w->type != W_LIST) return;
    for (int i = 0; i < w->n_items; i++) free(w->items[i]);
    w->n_items = 0; w->sel = -1; w->top = 0;
    mark(w);
}
int ui_list_count(ui_widget* w)    { return w && w->type == W_LIST ? w->n_items : 0; }
int ui_list_selected(ui_widget* w) { return w && w->type == W_LIST ? w->sel : -1; }
void ui_list_set_selected(ui_widget* w, int index) {
    if (!w || w->type != W_LIST) return;
    list_select(w, index);
}
const char* ui_list_item(ui_widget* w, int index) {
    if (!w || w->type != W_LIST || index < 0 || index >= w->n_items) return NULL;
    return w->items[index];
}
void ui_list_set_rows(ui_widget* w, int rows) { if (w && w->type == W_LIST && rows > 0) { w->want_rows = rows; mark(w); } }

ui_widget* ui_progress(ui_widget* parent) { return new_widget(parent, W_PROGRESS); }
void ui_progress_set(ui_widget* w, int percent) {
    if (w && w->type == W_PROGRESS) { w->value = clampi(percent, 0, 100); mark(w); }
}

ui_widget* ui_slider(ui_widget* parent, int min, int max, int value, ui_cb on_change, void* ctx) {
    ui_widget* w = new_widget(parent, W_SLIDER);
    if (!w) return NULL;
    if (max < min) max = min;
    w->vmin = min; w->vmax = max; w->value = clampi(value, min, max);
    w->cb = on_change; w->ctx = ctx;
    return w;
}
int  ui_slider_get(ui_widget* w) { return w && w->type == W_SLIDER ? w->value : 0; }
void ui_slider_set(ui_widget* w, int value) {
    if (w && w->type == W_SLIDER) { w->value = clampi(value, w->vmin, w->vmax); mark(w); }
}

ui_widget* ui_separator(ui_widget* parent) { return new_widget(parent, W_SEP); }
ui_widget* ui_spacer(ui_widget* parent) {
    ui_widget* w = new_widget(parent, W_SPACER);
    if (w) w->expand = 1;
    return w;
}

void ui_on_change(ui_widget* w, ui_cb cb, void* ctx) { if (w) { w->cb_change = cb; w->ctx_change = ctx; } }
void ui_set_enabled(ui_widget* w, int enabled) {
    if (!w) return;
    w->enabled = enabled ? 1 : 0;
    if (!w->enabled && w->app->focus == w) w->app->focus = NULL;
    mark(w);
}
void ui_set_visible(ui_widget* w, int visible) { if (w) { w->visible = visible ? 1 : 0; mark(w); } }
void ui_set_expand(ui_widget* w, int weight)   { if (w) { w->expand = weight < 0 ? 0 : weight; mark(w); } }
void ui_set_size(ui_widget* w, int width, int height) {
    if (!w) return;
    w->fw = width > 0 ? width : 0;
    w->fh = height > 0 ? height : 0;
    mark(w);
}
void ui_focus(ui_widget* w) { if (w && is_focusable(w)) { w->app->focus = w; w->app->blink = 1; mark(w); } }
void ui_get_rect(ui_widget* w, int* x, int* y, int* width, int* height) {
    if (!w) return;
    if (x) *x = w->x;
    if (y) *y = w->y;
    if (width) *width = w->w;
    if (height) *height = w->h;
}
ui_app* ui_app_of(ui_widget* w) { return w ? w->app : NULL; }
