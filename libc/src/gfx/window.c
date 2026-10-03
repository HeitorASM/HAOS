#include "haos/gfx.h"
#include "haos/syscall.h"
#include "string.h"

// Structs de troca com o kernel (espelham gui/core/userwin.h).
struct win_rect  { int32_t x, y, w, h; uint32_t color; };
struct win_text  { int32_t x, y; uint32_t fg, bg, flags, len; uint64_t text_ptr; };
struct win_blit  { int32_t x, y, w, h; uint64_t pixels_ptr; };
struct win_info  { uint32_t w, h; };

int win_create(unsigned w, unsigned h, const char* title) {
    long len = title ? (long)strlen(title) : 0;
    return (int)haos_syscall(SYS_WIN_CREATE, (long)w, (long)h, (long)title, len, 0);
}

int win_destroy(int win) {
    return (int)haos_syscall(SYS_WIN_DESTROY, win, 0, 0, 0, 0);
}

int win_size(int win, unsigned* w, unsigned* h) {
    struct win_info info;
    if (!w || !h) return -1;
    if (haos_syscall(SYS_WIN_INFO, win, (long)&info, 0, 0, 0) != 0) return -1;
    *w = info.w;
    *h = info.h;
    return 0;
}

int win_fill_rect(int win, int x, int y, int w, int h, uint32_t color) {
    struct win_rect r = { x, y, w, h, color };
    return (int)haos_syscall(SYS_WIN_FILL, win, (long)&r, 0, 0, 0);
}

static int text_common(int win, int x, int y, const char* s,
                       uint32_t fg, uint32_t bg, uint32_t flags) {
    if (!s) return -1;
    struct win_text t;
    t.x = x; t.y = y; t.fg = fg; t.bg = bg; t.flags = flags;
    t.len = (uint32_t)strlen(s);
    t.text_ptr = (uint64_t)(uintptr_t)s;
    return (int)haos_syscall(SYS_WIN_TEXT, win, (long)&t, 0, 0, 0);
}

int win_text(int win, int x, int y, const char* s, uint32_t fg) {
    return text_common(win, x, y, s, fg, 0, 0);
}

int win_text_bg(int win, int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    return text_common(win, x, y, s, fg, bg, WIN_TEXT_OPAQUE);
}

int win_blit(int win, int x, int y, int w, int h, const uint32_t* pixels) {
    if (!pixels) return -1;
    struct win_blit b;
    b.x = x; b.y = y; b.w = w; b.h = h;
    b.pixels_ptr = (uint64_t)(uintptr_t)pixels;
    return (int)haos_syscall(SYS_WIN_BLIT, win, (long)&b, 0, 0, 0);
}

int win_event(int win, struct haos_win_event* ev, unsigned timeout_ms) {
    if (!ev) return -1;
    return (int)haos_syscall(SYS_WIN_EVENT, win, (long)ev, (long)timeout_ms, 0, 0);
}
