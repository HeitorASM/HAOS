// examples/imgview/main.c — visualizador de imagens (PNG e BMP).
//
//   run imgview.hexec /share/images/gradiente.png
//   (ou: duplo-clique numa imagem no Explorador de Arquivos)
//
//   f        ajusta a imagem à janela          1        tamanho real (100%)
//   + / -    zoom (a roda do mouse também)     setas    move a imagem
//   arrastar com o mouse move a imagem         PgDn / . próxima imagem da pasta
//   PgUp / , imagem anterior                   w        usar como papel de parede
//   q        sair
//
//   imgview --wallpaper <arquivo>   (sem janela) define a imagem como papel de
//                                   parede e termina — usado pelo Explorador.
//
// O decodificador mora em imgdec.h (inflate + PNG + BMP); imagens com
// transparência aparecem sobre um xadrez.

#include "haos/gfx.h"
#include "haos/fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "imgdec.h"

#define STATUS_H   20
#define BG         0x0F1220
#define BAR_BG     0x1A2238
#define BAR_FG     0xAABBCC
#define ERR_FG     0xFF8888
#define MAX_NAMES  128
#define ZOOM_MIN   20
#define ZOOM_MAX   8000

static int      win;
static unsigned cw, ch;                       // área cliente
static Img      img;
static int      have_img;
static char     g_err[96];
static char     g_dir[256];
static char     g_names[MAX_NAMES][HAOS_NAME_MAX];
static int      g_nnames, g_cur;
static char     g_note[72];                   // aviso na barra de status (some na próxima tecla)
static int      zoom = 1000;                  // 1000 = 100%
static int      fit  = 1;
static int      ox, oy;                       // canto da imagem na área de desenho
static uint32_t* vbuf;
static size_t    vbuf_cap;
static int*      xmap;
static size_t    xmap_cap;

// ---------- utilidades de texto/caminho ----------
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int is_image_name(const char* n) {
    size_t l = strlen(n);
    if (l < 5) return 0;
    const char* e = n + l - 4;
    return e[0] == '.' && ((lower(e[1]) == 'p' && lower(e[2]) == 'n' && lower(e[3]) == 'g') ||
                           (lower(e[1]) == 'b' && lower(e[2]) == 'm' && lower(e[3]) == 'p'));
}

static void set_err(const char* msg) {
    strncpy(g_err, msg, sizeof(g_err) - 1);
    g_err[sizeof(g_err) - 1] = 0;
}

static void join_path(char* out, size_t cap, const char* dir, const char* name) {
    size_t l = strlen(dir);
    if (l + 1 + strlen(name) + 1 > cap) { out[0] = 0; return; }
    strcpy(out, dir);
    if (l == 0 || out[l - 1] != '/') strcat(out, "/");
    strcat(out, name);
}

// ---------- pasta: lista de imagens irmãs ----------
static void list_siblings(const char* path) {
    g_nnames = 0; g_cur = 0;
    const char* slash = NULL;
    for (const char* p = path; *p; p++) if (*p == '/') slash = p;
    const char* base = slash ? slash + 1 : path;
    if (!slash)                 strcpy(g_dir, ".");
    else if (slash == path)     strcpy(g_dir, "/");
    else {
        size_t l = (size_t)(slash - path);
        if (l >= sizeof(g_dir)) l = sizeof(g_dir) - 1;
        memcpy(g_dir, path, l);
        g_dir[l] = 0;
    }
    struct haos_dirent e;
    for (unsigned long i = 0; g_nnames < MAX_NAMES && readdir(g_dir, i, &e) == 1; i++) {
        if (e.type != HAOS_TYPE_FILE || !is_image_name(e.name)) continue;
        strcpy(g_names[g_nnames++], e.name);
    }
    for (int i = 1; i < g_nnames; i++) {                 // ordena por nome (inserção)
        char tmp[HAOS_NAME_MAX];
        strcpy(tmp, g_names[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(g_names[j], tmp) > 0) { strcpy(g_names[j + 1], g_names[j]); j--; }
        strcpy(g_names[j + 1], tmp);
    }
    for (int i = 0; i < g_nnames; i++) if (strcmp(g_names[i], base) == 0) g_cur = i;
}

// ---------- arquivo -> imagem ----------
static int read_file(const char* path, uint8_t** out, size_t* len) {
    struct haos_stat st;
    if (stat(path, &st) != 0 || st.type != HAOS_TYPE_FILE) { set_err("arquivo nao encontrado"); return -1; }
    if (st.size == 0) { set_err("arquivo vazio"); return -1; }
    uint8_t* buf = (uint8_t*)malloc(st.size);
    if (!buf) { set_err("sem memoria"); return -1; }
    int fd = open(path);
    if (fd < 0) { free(buf); set_err("sem permissao para abrir"); return -1; }
    size_t got = 0;
    while (got < st.size) {
        ssize_t r = read(fd, buf + got, st.size - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got != st.size) { free(buf); set_err("erro de leitura"); return -1; }
    *out = buf; *len = got;
    return 0;
}

static int load_image(const char* path) {
    if (have_img) { img_free(&img); have_img = 0; }
    uint8_t* data = NULL; size_t len = 0;
    if (read_file(path, &data, &len) != 0) return -1;
    const char* err = "erro desconhecido";
    int rc = img_load(data, len, &img, &err);
    free(data);
    if (rc != 0) { set_err(err); return -1; }
    have_img = 1;
    g_err[0] = 0;
    fit = 1;
    return 0;
}

// ---------- geometria ----------
static int view_w(void) { return (int)cw; }
static int view_h(void) { return (int)ch - STATUS_H; }

static void compute_fit(void) {
    if (!have_img) return;
    long zx = (long)view_w() * 1000 / img.w, zy = (long)view_h() * 1000 / img.h;
    long z = zx < zy ? zx : zy;
    if (z > 1000) z = 1000;                              // não amplia sozinho
    if (z < ZOOM_MIN) z = ZOOM_MIN;
    zoom = (int)z;
}

static void clamp_pan(void) {
    if (!have_img) return;
    long sw = (long)img.w * zoom / 1000, sh = (long)img.h * zoom / 1000;
    if (sw <= view_w()) ox = (int)((view_w() - sw) / 2);
    else { if (ox > 0) ox = 0; if (ox < view_w() - (int)sw) ox = view_w() - (int)sw; }
    if (sh <= view_h()) oy = (int)((view_h() - sh) / 2);
    else { if (oy > 0) oy = 0; if (oy < view_h() - (int)sh) oy = view_h() - (int)sh; }
}

// Muda o zoom mantendo fixo o ponto da imagem que está no centro da área.
static void set_zoom(int nz) {
    if (!have_img) return;
    if (nz < ZOOM_MIN) nz = ZOOM_MIN;
    if (nz > ZOOM_MAX) nz = ZOOM_MAX;
    long cx = ((long)view_w() / 2 - ox) * 1000 / zoom;
    long cy = ((long)view_h() / 2 - oy) * 1000 / zoom;
    zoom = nz;
    ox = (int)((long)view_w() / 2 - cx * zoom / 1000);
    oy = (int)((long)view_h() / 2 - cy * zoom / 1000);
    fit = 0;
    clamp_pan();
}

// ---------- desenho ----------
static void draw_status(void) {
    win_fill_rect(win, 0, view_h(), (int)cw, STATUS_H, BAR_BG);
    char line[200];
    if (g_note[0]) {
        win_text_bg(win, 6, view_h() + 2, g_note, 0xFFE070, BAR_BG);
    } else if (have_img) {
        const char* name = g_nnames ? g_names[g_cur] : "";
        snprintf(line, sizeof(line), "%s  %dx%d  %d%%  [%d/%d]   f:ajustar 1:100%% +/-:zoom w:fundo PgUp/PgDn q:sair",
                 name, img.w, img.h, zoom / 10, g_nnames ? g_cur + 1 : 1, g_nnames ? g_nnames : 1);
        win_text_bg(win, 6, view_h() + 2, line, BAR_FG, BAR_BG);
    } else {
        win_text_bg(win, 6, view_h() + 2, "q: sair", BAR_FG, BAR_BG);
    }
}

static void draw_message(void) {
    int vw = view_w(), vh = view_h();
    if (vw <= 0 || vh <= 0) return;
    win_fill_rect(win, 0, 0, vw, vh, BG);
    win_text(win, 12, 12, "Nao foi possivel abrir a imagem:", 0xFFFFFF);
    win_text(win, 12, 32, g_err[0] ? g_err : "(sem arquivo informado)", ERR_FG);
    win_text(win, 12, 64, "Abra uma imagem PNG ou BMP pelo Explorador de Arquivos.", BAR_FG);
}

static void render(void) {
    int vw = view_w(), vh = view_h();
    if (vw <= 0 || vh <= 0) return;
    if (!have_img) { draw_message(); draw_status(); return; }

    size_t need = (size_t)vw * (size_t)vh;
    if (need > vbuf_cap) {
        free(vbuf);
        vbuf = (uint32_t*)malloc(need * 4);
        vbuf_cap = vbuf ? need : 0;
    }
    if ((size_t)vw > xmap_cap) {
        free(xmap);
        xmap = (int*)malloc((size_t)vw * sizeof(int));
        xmap_cap = xmap ? (size_t)vw : 0;
    }
    if (!vbuf || !xmap) { win_text(win, 12, 12, "sem memoria", ERR_FG); return; }

    for (int x = 0; x < vw; x++) {
        long t = (long)(x - ox) * 1000;
        int ix = t < 0 ? -1 : (int)(t / zoom);
        xmap[x] = ix >= img.w ? -1 : ix;
    }
    for (int y = 0; y < vh; y++) {
        uint32_t* out = vbuf + (size_t)y * (size_t)vw;
        long t = (long)(y - oy) * 1000;
        int iy = t < 0 ? -1 : (int)(t / zoom);
        if (iy < 0 || iy >= img.h) {
            for (int x = 0; x < vw; x++) out[x] = BG;
            continue;
        }
        const uint32_t* src = img.px + (size_t)iy * (size_t)img.w;
        for (int x = 0; x < vw; x++) {
            int ix = xmap[x];
            if (ix < 0) { out[x] = BG; continue; }
            uint32_t p = src[ix];
            uint32_t a = p >> 24;
            if (a == 255) { out[x] = p & 0xFFFFFF; continue; }
            uint32_t bgc = (((x >> 3) ^ (y >> 3)) & 1) ? 0x3A3F4F : 0x2B2F3C;     // xadrez
            uint32_t r = (((p >> 16) & 255) * a + ((bgc >> 16) & 255) * (255 - a)) / 255;
            uint32_t g = (((p >> 8) & 255) * a + ((bgc >> 8) & 255) * (255 - a)) / 255;
            uint32_t b = ((p & 255) * a + (bgc & 255) * (255 - a)) / 255;
            out[x] = (r << 16) | (g << 8) | b;
        }
    }
    win_blit(win, 0, 0, vw, vh, vbuf);
    draw_status();
}

// ---------- papel de parede ----------
// Reduz (média da área) para caber em 1024x768 mantendo a proporção, compõe a
// transparência sobre preto e entrega ao kernel (gfx_set_wallpaper).
static int set_wallpaper_from(const Img* im) {
    int tw = im->w, th = im->h;
    if (tw > 1024 || th > 768) {
        if ((long)tw * 768 > (long)th * 1024) { th = (int)((long)th * 1024 / tw); tw = 1024; }
        else                                  { tw = (int)((long)tw * 768 / th);  th = 768; }
    }
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    uint32_t* out = (uint32_t*)malloc((size_t)tw * (size_t)th * 4);
    if (!out) return -1;
    for (int y = 0; y < th; y++) {
        int sy0 = (int)((long)y * im->h / th), sy1 = (int)((long)(y + 1) * im->h / th);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < tw; x++) {
            int sx0 = (int)((long)x * im->w / tw), sx1 = (int)((long)(x + 1) * im->w / tw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned long r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < im->h; sy++)
                for (int sx = sx0; sx < sx1 && sx < im->w; sx++) {
                    uint32_t p = im->px[(size_t)sy * (size_t)im->w + (size_t)sx];
                    uint32_t a = p >> 24;
                    r += ((p >> 16) & 255) * a / 255;       // alfa sobre preto
                    g += ((p >> 8) & 255) * a / 255;
                    b += (p & 255) * a / 255;
                    n++;
                }
            if (n == 0) n = 1;
            out[(size_t)y * (size_t)tw + (size_t)x] = (uint32_t)((r / n) << 16 | (g / n) << 8 | (b / n));
        }
    }
    int rc = gfx_set_wallpaper(out, tw, th);
    free(out);
    return rc;
}

static void note(const char* msg) {
    strncpy(g_note, msg, sizeof(g_note) - 1);
    g_note[sizeof(g_note) - 1] = 0;
}

// ---------- título da janela ----------
static void update_title(void) {
    if (!have_img || !g_nnames) return;
    char t[64];
    snprintf(t, sizeof(t), "%s - Visualizador", g_names[g_cur]);
    win_set_title(win, t);
}

// ---------- troca de imagem ----------
static void open_sibling(int delta) {
    if (g_nnames < 2) return;
    int n = g_cur + delta;
    if (n < 0) n = g_nnames - 1;
    if (n >= g_nnames) n = 0;
    char path[320];
    join_path(path, sizeof(path), g_dir, g_names[n]);
    g_cur = n;
    load_image(path);                    // em caso de erro, have_img = 0 e a mensagem aparece
    compute_fit();
    clamp_pan();
    update_title();
}

// ---------- eventos ----------
static int zoom_up(int z)   { int n = z * 5 / 4; if (n == z) n = z + 1; return n; }
static int zoom_down(int z) { int n = z * 4 / 5; if (n == z) n = z - 1; return n; }

static int dragging, last_x, last_y;

// Retorna 1 para sair. *dirty = 1 se precisa redesenhar.
static int handle(const struct haos_win_event* ev, int* dirty) {
    switch (ev->type) {
        case HAOS_EV_KEY: {
            int k = ev->a;
            if (g_note[0]) { g_note[0] = 0; *dirty = 1; }
            if (k == 'q' || k == 'Q') return 1;
            if ((k == 'w' || k == 'W') && have_img) {
                note("Definindo papel de parede...");
                render();                                    // mostra o aviso já (salvar demora um pouco)
                note(set_wallpaper_from(&img) == 0 ? "Papel de parede definido."
                                                    : "Nao foi possivel definir o papel de parede.");
                *dirty = 1;
                break;
            }
            if (k == '+' || k == '=') { set_zoom(zoom_up(zoom)); *dirty = 1; }
            else if (k == '-' || k == '_') { set_zoom(zoom_down(zoom)); *dirty = 1; }
            else if (k == 'f' || k == 'F') { fit = 1; compute_fit(); clamp_pan(); *dirty = 1; }
            else if (k == '1') { set_zoom(1000); *dirty = 1; }
            else if (k == HAOS_KEY_LEFT)  { ox += 40; clamp_pan(); *dirty = 1; }
            else if (k == HAOS_KEY_RIGHT) { ox -= 40; clamp_pan(); *dirty = 1; }
            else if (k == HAOS_KEY_UP)    { oy += 40; clamp_pan(); *dirty = 1; }
            else if (k == HAOS_KEY_DOWN)  { oy -= 40; clamp_pan(); *dirty = 1; }
            else if (k == HAOS_KEY_PAGE_DOWN || k == '.') { open_sibling(+1); *dirty = 1; }
            else if (k == HAOS_KEY_PAGE_UP   || k == ',') { open_sibling(-1); *dirty = 1; }
            break;
        }
        case HAOS_EV_MOUSE_DOWN: dragging = 1; last_x = ev->a; last_y = ev->b; break;
        case HAOS_EV_MOUSE_UP:   dragging = 0; break;
        case HAOS_EV_MOUSE_MOVE:
            if (dragging && ev->c == 1) {
                ox += ev->a - last_x; oy += ev->b - last_y;
                last_x = ev->a; last_y = ev->b;
                clamp_pan();
                *dirty = 1;
            }
            break;
        case HAOS_EV_MOUSE_SCROLL:
            set_zoom(ev->c > 0 ? zoom_up(zoom) : zoom_down(zoom));
            *dirty = 1;
            break;
        case HAOS_EV_RESIZE:
            cw = (unsigned)ev->a; ch = (unsigned)ev->b;
            if (fit) compute_fit();
            clamp_pan();
            *dirty = 1;
            break;
        default: break;
    }
    return 0;
}

int main(int argc, char** argv) {
    g_err[0] = 0;

    // Modo sem janela: imgview --wallpaper <arquivo>
    if (argc >= 3 && argv[1] && strcmp(argv[1], "--wallpaper") == 0) {
        if (load_image(argv[2]) != 0) return 1;
        int rc = set_wallpaper_from(&img);
        img_free(&img);
        return rc == 0 ? 0 : 1;
    }

    if (argc >= 2 && argv[1] && argv[1][0]) {
        list_siblings(argv[1]);
        load_image(argv[1]);
    }

    unsigned want_w = 420, want_h = 140 + STATUS_H;
    if (have_img) {
        want_w = (unsigned)img.w < 900 ? (unsigned)img.w : 900;
        want_h = ((unsigned)img.h < 620 ? (unsigned)img.h : 620) + STATUS_H;
        if (want_w < 360) want_w = 360;                  // cabe a barra de status mínima
    }
    win = win_create(want_w, want_h, "Visualizador de imagens");
    if (win < 0) return 1;
    win_size(win, &cw, &ch);
    update_title();
    compute_fit();
    clamp_pan();
    render();

    struct haos_win_event ev;
    for (;;) {
        int r = win_event(win, &ev, WIN_WAIT_FOREVER);
        if (r < 0) break;                                // janela fechada
        if (r == 0) continue;
        int dirty = 0;
        if (handle(&ev, &dirty)) break;
        int quit = 0;
        while ((r = win_event(win, &ev, 0)) == 1) {      // junta o que já chegou: 1 redesenho só
            if (handle(&ev, &dirty)) { quit = 1; break; }
        }
        if (quit || r < 0) break;
        if (dirty) render();
    }
    win_destroy(win);
    if (have_img) img_free(&img);
    return 0;
}
