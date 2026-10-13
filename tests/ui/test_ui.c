// Teste de host do toolkit de widgets (libc/src/gfx/ui.c + ui_dialog.c + draw.c + uitheme.c).
// O "kernel" é substituído por stubs: uma janela falsa cujos eventos vêm de um ROTEIRO
// (cliques/teclas/rodinha), e cada quadro apresentado é guardado para inspecionar pixels.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "haos/ui.h"
#include "haos/gfx.h"

static int checks = 0, fails = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("  FALHOU: %s (linha %d)\n", #c, __LINE__); } } while (0)

// ---------------------------------------------------------------- stubs
static uint32_t g_theme[UI_THEME_COUNT];
static unsigned g_created_w, g_created_h, g_client_w, g_client_h;
static unsigned long g_now = 1000;
static char g_clip[600]; static long g_clip_len;
static uint32_t* g_frame; static int g_fw, g_fh, g_frames;
static int g_destroyed;

static void theme_init(void) {
    memset(g_theme, 0, sizeof(g_theme));
    g_theme[UI_THEME_GENERATION] = 1;      g_theme[UI_WINDOW_BG] = 0x101010;   g_theme[UI_PANEL_BG] = 0x202020;
    g_theme[UI_PANEL_BORDER] = 0x404040;   g_theme[UI_TEXT] = 0xEEEEEE;        g_theme[UI_TEXT_SECONDARY] = 0xCCCCCC;
    g_theme[UI_TEXT_DIM] = 0x777777;       g_theme[UI_ACCENT] = 0x3070FF;      g_theme[UI_ACCENT_DARK] = 0x1040C0;
    g_theme[UI_BUTTON_BG] = 0x303A50;      g_theme[UI_BUTTON_HOVER] = 0x405070; g_theme[UI_BUTTON_PRESSED] = 0x202A40;
    g_theme[UI_BUTTON_FG] = 0xFFFFFF;      g_theme[UI_BUTTON_BORDER] = 0x506080; g_theme[UI_BUTTON_BORDER_FOCUS] = 0xFFFF00;
    g_theme[UI_FIELD_BG] = 0x080808;       g_theme[UI_FIELD_FG] = 0xFFFFFF;    g_theme[UI_FIELD_BORDER] = 0x445566;
    g_theme[UI_FIELD_BORDER_FOCUS] = 0x00FFFF; g_theme[UI_FIELD_CURSOR] = 0xFFFFFF;
    g_theme[UI_CHECK_BG] = 0x0A0A0A;       g_theme[UI_CHECK_BORDER] = 0x667788; g_theme[UI_CHECK_MARK] = 0x00FF00;
    g_theme[UI_LIST_BG] = 0x111111;        g_theme[UI_LIST_ITEM] = 0x181818;   g_theme[UI_LIST_SELECTED] = 0x2040A0;
    g_theme[UI_LIST_FG] = 0xDDDDDD;        g_theme[UI_BUTTONS_ROUNDED] = 0;    g_theme[UI_BUTTON_RADIUS] = 4;
    g_theme[UI_SPACING_SM] = 4; g_theme[UI_SPACING_MD] = 8; g_theme[UI_SPACING_LG] = 12; g_theme[UI_WIDGET_HEIGHT] = 28;
}

int gfx_theme_get(uint32_t* out, unsigned count) {
    for (unsigned i = 0; i < count && i < UI_THEME_COUNT; i++) out[i] = g_theme[i];
    return UI_THEME_COUNT;
}
int gfx_font_data(uint8_t* out) { memset(out, 0, 4096); return 4096; }
long clip_get(char* buf, unsigned long cap) {
    if (buf) { unsigned long n = (unsigned long)g_clip_len < cap ? (unsigned long)g_clip_len : cap; memcpy(buf, g_clip, n); }
    return g_clip_len;
}
int clip_set(const char* d, unsigned long len) {
    if (len > sizeof(g_clip)) len = sizeof(g_clip);
    memcpy(g_clip, d, len); g_clip_len = (long)len; return 0;
}
unsigned long uptime_ms(void) { return g_now; }
int win_create(unsigned w, unsigned h, const char* t) { (void)t; g_created_w = w; g_created_h = h; g_destroyed = 0; return 3; }
int win_destroy(int win) { (void)win; g_destroyed = 1; return 0; }
int win_size(int win, unsigned* w, unsigned* h) {
    (void)win;
    *w = g_client_w ? g_client_w : g_created_w;
    *h = g_client_h ? g_client_h : g_created_h;
    return 0;
}
int win_set_title(int win, const char* t) { (void)win; (void)t; return 0; }
int win_blit(int win, int x, int y, int w, int h, const uint32_t* px) {
    (void)win; (void)x; (void)y;
    free(g_frame);
    g_frame = (uint32_t*)malloc((size_t)w * (size_t)h * 4);
    memcpy(g_frame, px, (size_t)w * (size_t)h * 4);
    g_fw = w; g_fh = h; g_frames++;
    return 0;
}
static uint32_t pix(int x, int y) { return g_frame ? g_frame[(size_t)y * (size_t)g_fw + (size_t)x] : 0xDEAD; }

// ---------------------------------------------------------------- roteiro de eventos
enum { S_CLICK_C = 1, S_CLICK, S_DOWN, S_MOVE, S_UP, S_WHEEL, S_KEY, S_EVENT, S_TIME, S_FN, S_RENDER };
typedef struct { int kind; ui_widget* w; int a, b, c, d; void (*fn)(void); } Step;
static Step g_steps[160];
static int g_n, g_si, g_pending_up, g_ux, g_uy;

static void reset_script(void) { g_n = g_si = g_pending_up = 0; g_client_w = g_client_h = 0; }
static void add(int kind, ui_widget* w, int a, int b, int c, int d, void (*fn)(void)) {
    Step s = { kind, w, a, b, c, d, fn };
    g_steps[g_n++] = s;
}
#define CLICK_C(w)           add(S_CLICK_C, w, 0, 0, 0, 0, 0)     /* clique no centro */
#define CLICK(w, dx, dy)     add(S_CLICK, w, dx, dy, 0, 0, 0)     /* clique a (dx,dy) do canto sup. esq. */
#define DOWN(w, dx, dy)      add(S_DOWN, w, dx, dy, 0, 0, 0)
#define MOVE(w, dx, dy, btn) add(S_MOVE, w, dx, dy, btn, 0, 0)
#define UP()                 add(S_UP, 0, 0, 0, 0, 0, 0)
#define WHEEL(w, amount)     add(S_WHEEL, w, 0, 0, amount, 0, 0)
#define KEY(k)               add(S_KEY, 0, k, 0, 0, 0, 0)
#define EVENT(t, a_, b_, c_) add(S_EVENT, 0, t, a_, b_, c_, 0)
#define TIME(ms)             add(S_TIME, 0, ms, 0, 0, 0, 0)
#define FN(f)                add(S_FN, 0, 0, 0, 0, 0, f)          /* roda f() e força um quadro */
#define RENDER()             add(S_RENDER, 0, 0, 0, 0, 0, 0)
/* Executa o trecho de roteiro montado ate aqui (ui_run), termina com um quadro e zera o roteiro.
   Os CHECKs vem DEPOIS do RUN: o roteiro so "acontece" dentro de ui_run(). */
#define RUN(app)             do { add(S_RENDER, 0, 0, 0, 0, 0, 0); ui_run(app); g_n = g_si = g_pending_up = 0; } while (0)

static void rect_pt(ui_widget* w, int dx, int dy, int center, int* x, int* y) {
    int rx, ry, rw, rh;
    ui_get_rect(w, &rx, &ry, &rw, &rh);
    *x = center ? rx + rw / 2 : rx + dx;
    *y = center ? ry + rh / 2 : ry + dy;
}

int win_event(int win, struct haos_win_event* ev, unsigned timeout) {
    (void)win; (void)timeout;
    memset(ev, 0, sizeof(*ev));
    if (g_pending_up) { g_pending_up = 0; ev->type = HAOS_EV_MOUSE_UP; ev->a = g_ux; ev->b = g_uy; return 1; }
    if (g_si >= g_n) return -1;                                  // roteiro acabou: "janela fechada"
    Step* s = &g_steps[g_si++];
    switch (s->kind) {
    case S_CLICK_C: rect_pt(s->w, 0, 0, 1, &g_ux, &g_uy); ev->type = HAOS_EV_MOUSE_DOWN; ev->a = g_ux; ev->b = g_uy; g_pending_up = 1; return 1;
    case S_CLICK:   rect_pt(s->w, s->a, s->b, 0, &g_ux, &g_uy); ev->type = HAOS_EV_MOUSE_DOWN; ev->a = g_ux; ev->b = g_uy; g_pending_up = 1; return 1;
    case S_DOWN:    rect_pt(s->w, s->a, s->b, 0, &g_ux, &g_uy); ev->type = HAOS_EV_MOUSE_DOWN; ev->a = g_ux; ev->b = g_uy; return 1;
    case S_MOVE:    rect_pt(s->w, s->a, s->b, 0, &g_ux, &g_uy); ev->type = HAOS_EV_MOUSE_MOVE; ev->a = g_ux; ev->b = g_uy; ev->c = s->c; return 1;
    case S_UP:      ev->type = HAOS_EV_MOUSE_UP; ev->a = g_ux; ev->b = g_uy; return 1;
    case S_WHEEL:   rect_pt(s->w, 0, 0, 1, &g_ux, &g_uy); ev->type = HAOS_EV_MOUSE_SCROLL; ev->a = g_ux; ev->b = g_uy; ev->c = s->c; return 1;
    case S_KEY:     ev->type = HAOS_EV_KEY; ev->a = s->a; return 1;
    case S_EVENT:   ev->type = (uint32_t)s->a; ev->a = s->b; ev->b = s->c; ev->c = s->d; return 1;
    case S_TIME:    g_now = (unsigned long)s->a; return 0;
    case S_FN:      s->fn(); return 0;
    default:        return 0;                                    // S_RENDER: sem evento -> redesenha
    }
}

// ---------------------------------------------------------------- utilidades dos testes
static ui_app* new_app(int w, int h) {
    reset_script(); theme_init();
    g_client_w = (unsigned)w; g_client_h = (unsigned)h;
    return ui_app_new("teste", w, h);
}
static int g_hits, g_hits2, g_changes, g_key_got, g_ticks;
static void cb_hit(ui_widget* w, void* ctx)  { (void)w; (void)ctx; g_hits++; }
static void cb_hit2(ui_widget* w, void* ctx) { (void)w; (void)ctx; g_hits2++; }
static void cb_chg(ui_widget* w, void* ctx)  { (void)w; (void)ctx; g_changes++; }
static int  cb_key(ui_app* ap, int k, void* cx) { (void)ap; (void)cx; g_key_got = k; return 1; }
static void cb_tick(ui_app* ap, void* cx) { (void)ap; (void)cx; g_ticks++; }
static void rect(ui_widget* w, int* x, int* y, int* ww, int* h) { ui_get_rect(w, x, y, ww, h); }

// ---------------------------------------------------------------- testes
static void test_layout(void) {
    printf("[1] layout: vbox/hbox, espacamento, expand\n");
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* lab = ui_label(root, "Hello");
    ui_widget* b1 = ui_button(root, "OK", 0, 0);
    ui_widget* row = ui_hbox(root, 4);
    ui_widget* ba = ui_button(row, "A", 0, 0);
    ui_widget* bb = ui_button(row, "B", 0, 0);
    ui_set_expand(ba, 1); ui_set_expand(bb, 1);
    ui_widget* sp = ui_spacer(root);
    ui_widget* last = ui_button(root, "Last", 0, 0);
    RUN(app);
    int x, y, w, h;
    rect(root, &x, &y, &w, &h);  CHECK(x == 0 && y == 0 && w == 300 && h == 200);
    rect(lab, &x, &y, &w, &h);   CHECK(x == 12 && y == 12 && w == 276 && h == 16);
    rect(b1, &x, &y, &w, &h);    CHECK(x == 12 && y == 36 && w == 276 && h == 28);
    rect(row, &x, &y, &w, &h);   CHECK(x == 12 && y == 72 && w == 276 && h == 28);
    rect(ba, &x, &y, &w, &h);    CHECK(x == 12 && w == 136 && h == 28);
    rect(bb, &x, &y, &w, &h);    CHECK(x == 152 && w == 136);
    rect(sp, &x, &y, &w, &h);    CHECK(y == 108 && h == 44);
    rect(last, &x, &y, &w, &h);  CHECK(y == 160 && h == 28 && y + h == 188);
    CHECK(pix(0, 0) == 0x101010);                  // fundo = cor do tema
    CHECK(pix(12 + 3, 36 + 3) == 0x303A50 || pix(12 + 3, 36 + 3) == 0x405070);   // dentro do botao: cor do botao
    ui_app_free(app);
    CHECK(g_destroyed == 1);

    printf("    tamanho fixo e widgets escondidos\n");
    app = new_app(300, 200);
    root = ui_root(app);
    ui_widget* f = ui_button(root, "Fixo", 0, 0);
    ui_set_size(f, 100, 40);
    ui_widget* hid = ui_button(root, "Escondido", 0, 0);
    ui_set_visible(hid, 0);
    ui_widget* nx = ui_button(root, "Proximo", 0, 0);
    RUN(app);
    rect(f, &x, &y, &w, &h);   CHECK(w == 100 && h == 40 && x == 12 && y == 12);
    rect(nx, &x, &y, &w, &h);  CHECK(y == 12 + 40 + 8);                    // o escondido nao ocupa espaco
    ui_app_free(app);
}

static void test_buttons(void) {
    printf("[2] botoes: clique, soltar fora, desabilitado, teclado, Tab\n");
    g_hits = g_hits2 = 0;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* b1 = ui_button(root, "Um", cb_hit, 0);
    ui_widget* b2 = ui_button(root, "Dois", cb_hit2, 0);
    ui_widget* b3 = ui_button(root, "Tres", cb_hit, 0);
    ui_set_enabled(b3, 0);

    CLICK_C(b1);
    RUN(app);
    CHECK(g_hits == 1);
    CLICK_C(b3);                                    // desabilitado: nada
    RUN(app);
    CHECK(g_hits == 1);
    DOWN(b2, 5, 5); MOVE(b1, 5, 5, 1); UP();       // pressiona em b2 e solta em b1: ninguem ativa
    RUN(app);
    CHECK(g_hits == 1 && g_hits2 == 0);
    KEY(' ');                                       // o foco foi para b2 (o ultimo clicado)
    RUN(app);
    CHECK(g_hits2 == 1);
    KEY('\t'); KEY('\n');                           // Tab: b3 desabilitado e pulado -> volta a b1
    RUN(app);
    CHECK(g_hits == 2 && g_hits2 == 1);
    ui_app_free(app);
}

static void test_check_radio(void) {
    printf("[3] caixa de selecao e opcoes\n");
    g_hits = g_changes = 0;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* ck = ui_check(root, "Ativar", 0, cb_hit, 0);
    ui_widget* r1 = ui_radio(root, "Opcao A", 1, cb_chg, 0);
    ui_widget* r2 = ui_radio(root, "Opcao B", 1, cb_chg, 0);
    ui_widget* r3 = ui_radio(root, "Outro grupo", 2, cb_chg, 0);
    ui_radio_set(r1); ui_radio_set(r3);
    CLICK_C(ck);
    RUN(app);
    CHECK(ui_check_get(ck) == 1 && g_hits == 1);
    CLICK_C(ck);
    RUN(app);
    CHECK(ui_check_get(ck) == 0 && g_hits == 2);
    CLICK_C(r2);
    RUN(app);
    CHECK(ui_radio_get(r1) == 0 && ui_radio_get(r2) == 1 && ui_radio_get(r3) == 1);   // grupo 2 intacto
    CLICK_C(r2);                                    // ja marcado: sem novo callback
    RUN(app);
    CHECK(g_changes == 1);
    KEY(HAOS_KEY_UP);                               // o foco esta em r2: a seta troca a opcao no grupo
    RUN(app);
    CHECK(ui_radio_get(r1) == 1 && ui_radio_get(r2) == 0 && ui_radio_get(r3) == 1);

    ui_check_set(ck, 1);
    RUN(app);
    int x, y, w, h, found = 0;                      // a marca verde desenhada dentro da caixa
    ui_get_rect(ck, &x, &y, &w, &h);
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + 16; xx++) if (pix(xx, yy) == 0x00FF00) found++;
    CHECK(found > 4);
    ui_check_set(ck, 0);
    RUN(app);
    found = 0;
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + 16; xx++) if (pix(xx, yy) == 0x00FF00) found++;
    CHECK(found == 0);
    ui_app_free(app);
}

static void test_entry(void) {
    printf("[4] campo de texto\n");
    g_hits = g_changes = 0;
    g_clip_len = 0;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* en = ui_entry(root, "", cb_hit, 0);
    ui_on_change(en, cb_chg, 0);
    CLICK_C(en); KEY('h'); KEY('i');
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "hi") == 0 && g_changes == 2);
    KEY('\b');
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "h") == 0);
    KEY(HAOS_KEY_HOME); KEY('X');
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "Xh") == 0);
    KEY(HAOS_KEY_DELETE);                           // apaga o 'h' a direita do cursor
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "X") == 0);
    KEY(HAOS_KEY_END); KEY('a'); KEY(HAOS_KEY_LEFT); KEY(HAOS_KEY_LEFT); KEY('b');
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "bXa") == 0);
    KEY('\n');
    RUN(app);
    CHECK(g_hits == 1);                             // Enter = on_activate
    KEY(HAOS_KEY_CTRL_C);
    RUN(app);
    CHECK(g_clip_len == 3 && memcmp(g_clip, "bXa", 3) == 0);
    memcpy(g_clip, "12\n34", 5); g_clip_len = 5;
    KEY(HAOS_KEY_END); KEY(HAOS_KEY_CTRL_V);        // cola so a 1a linha
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "bXa12") == 0);
    KEY(HAOS_KEY_CTRL_X);
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "") == 0 && g_clip_len == 5 && memcmp(g_clip, "bXa12", 5) == 0);
    ui_app_free(app);

    printf("    limite de caracteres e rolagem horizontal\n");
    app = new_app(300, 200);
    root = ui_root(app);
    en = ui_entry(root, "abc", 0, 0);
    ui_entry_set_max(en, 5);
    CLICK_C(en); KEY(HAOS_KEY_END); KEY('1'); KEY('2'); KEY('3'); KEY('4');
    RUN(app);
    CHECK(strcmp(ui_entry_text(en), "abc12") == 0);               // so cabem 2 (ja ha 3 de 5)
    ui_entry_set_max(en, 255);
    ui_entry_set_text(en, "um texto bem comprido que nao cabe no campo de 276 pixels de largura nem a pau");
    RUN(app);                                                     // nao pode quebrar com texto maior que o campo
    CHECK(g_frame != NULL && strlen(ui_entry_text(en)) > 60);
    ui_app_free(app);
}

static void test_list(void) {
    printf("[5] lista: selecao, teclado, rolagem, duplo clique\n");
    g_hits = g_changes = 0;
    g_now = 1000;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* li = ui_list(root, cb_hit, 0);
    ui_on_change(li, cb_chg, 0);
    ui_list_set_rows(li, 3);
    char t[16];
    for (int i = 0; i < 10; i++) { snprintf(t, sizeof t, "item %d", i); CHECK(ui_list_add(li, t) == i); }
    CHECK(ui_list_count(li) == 10 && ui_list_selected(li) == -1);
    CHECK(strcmp(ui_list_item(li, 4), "item 4") == 0 && ui_list_item(li, 10) == NULL);
    CLICK(li, 10, 12);                                            // linha 0
    RUN(app);
    CHECK(ui_list_selected(li) == 0 && g_changes == 1);
    CLICK(li, 10, 52);                                            // linha 2
    RUN(app);
    CHECK(ui_list_selected(li) == 2 && g_changes == 2 && g_hits == 0);
    KEY(HAOS_KEY_DOWN);
    RUN(app);
    CHECK(ui_list_selected(li) == 3);
    KEY(HAOS_KEY_END);
    RUN(app);
    CHECK(ui_list_selected(li) == 9);
    KEY(HAOS_KEY_DOWN);                                           // ja no fim
    RUN(app);
    CHECK(ui_list_selected(li) == 9);
    KEY(HAOS_KEY_HOME);
    RUN(app);
    CHECK(ui_list_selected(li) == 0);
    WHEEL(li, -1);                                                // rodinha para baixo: top += 3
    CLICK(li, 10, 12);                                            // a 1a linha visivel agora e o item 3
    RUN(app);
    CHECK(ui_list_selected(li) == 3 && g_hits == 0);
    CLICK(li, 10, 12);                                            // mesmo item, dentro de 400 ms -> duplo clique
    RUN(app);
    CHECK(g_hits == 1);
    TIME(5000);
    CLICK(li, 10, 12);                                            // tarde demais: nao e duplo
    RUN(app);
    CHECK(g_hits == 1);
    KEY('\n');                                                    // Enter ativa a selecao
    RUN(app);
    CHECK(g_hits == 2);
    ui_list_clear(li);
    CHECK(ui_list_count(li) == 0 && ui_list_selected(li) == -1);
    RUN(app);
    ui_app_free(app);
}

static void test_slider_progress(void) {
    printf("[6] deslizante e progresso\n");
    g_changes = 0;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_widget* sl = ui_slider(root, 0, 100, 50, cb_chg, 0);
    ui_widget* pr = ui_progress(root);
    ui_progress_set(pr, 150);                                     // limitado a 100
    CHECK(ui_slider_get(sl) == 50);
    KEY(HAOS_KEY_RIGHT);                                          // passo = 100/20 = 5
    RUN(app);
    CHECK(ui_slider_get(sl) == 55 && g_changes == 1);
    KEY(HAOS_KEY_HOME);
    RUN(app);
    CHECK(ui_slider_get(sl) == 0);
    KEY(HAOS_KEY_END);
    RUN(app);
    CHECK(ui_slider_get(sl) == 100);
    CLICK(sl, 7, 10);                                             // extremo esquerdo da trilha
    RUN(app);
    CHECK(ui_slider_get(sl) == 0);
    DOWN(sl, 7, 10); MOVE(sl, 7 + 131, 10, 1); UP();              // arrastar ate o meio (262 / 2)
    RUN(app);
    CHECK(ui_slider_get(sl) == 50);
    ui_slider_set(sl, 500);
    CHECK(ui_slider_get(sl) == 100);
    RUN(app);
    int x, y, w, h;
    ui_get_rect(pr, &x, &y, &w, &h);
    CHECK(pix(x + w - 3, y + h / 2) == 0x3070FF);                 // 100%: preenchido ate a borda
    ui_app_free(app);
}

static void test_focus(void) {
    printf("[7] foco: Tab pula desabilitado/escondido, ciclo\n");
    g_hits = g_hits2 = 0;
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_button(root, "A", cb_hit, 0);
    ui_widget* b = ui_button(root, "B", cb_hit2, 0);
    ui_widget* c = ui_button(root, "C", cb_hit, 0);
    ui_button(root, "D", cb_hit2, 0);
    ui_set_enabled(b, 0);
    ui_set_visible(c, 0);
    KEY('\t'); KEY(' ');                                          // A -> (B e C pulados) -> D: ativa D
    KEY('\t'); KEY(' ');                                          // volta para A: ativa A
    RUN(app);
    CHECK(g_hits2 == 1 && g_hits == 1);
    ui_app_free(app);

    printf("    teclas nao tratadas vao para o key handler\n");
    app = new_app(300, 200);
    root = ui_root(app);
    ui_label(root, "so um rotulo");
    g_key_got = 0;
    ui_set_key_handler(app, cb_key, 0);
    KEY('q');
    RUN(app);
    CHECK(g_key_got == 'q');
    ui_app_free(app);
}

static void set_theme_blue(void) { g_theme[UI_THEME_GENERATION] = 2; g_theme[UI_WINDOW_BG] = 0x112233; }

static void test_theme_resize_timer(void) {
    printf("[8] evento de tema, resize e timer\n");
    ui_app* app = new_app(300, 200);
    ui_widget* root = ui_root(app);
    ui_button(root, "OK", 0, 0);
    FN(set_theme_blue);
    EVENT(HAOS_EV_THEME, 0, 0, 0);
    EVENT(HAOS_EV_RESIZE, 400, 250, 0);
    RUN(app);
    CHECK(pix(0, 0) == 0x112233);                                 // cor nova do tema aplicada
    int x, y, w, h;
    ui_get_rect(root, &x, &y, &w, &h);
    CHECK(w == 400 && h == 250 && g_fw == 400 && g_fh == 250);    // canvas recriado e layout refeito
    ui_app_free(app);

    g_ticks = 0;
    app = new_app(300, 200);
    root = ui_root(app);
    ui_label(root, "timer");
    g_now = 1000;
    ui_set_timer(app, 100, cb_tick, 0);
    TIME(1050);                                                   // cedo demais
    TIME(1150);                                                   // passou de 100 ms
    TIME(1300);                                                   // passou de novo
    RUN(app);
    CHECK(g_ticks == 2);
    ui_set_timer(app, 0, 0, 0);                                   // cancela
    TIME(9000);
    RUN(app);
    CHECK(g_ticks == 2);
    ui_app_free(app);
}

static void test_dialogs(void) {
    printf("[9] dialogos modais\n");
    char buf[32];

    reset_script(); theme_init();
    KEY('a'); KEY('b'); KEY('c'); KEY('\n');
    strcpy(buf, "");
    CHECK(ui_prompt("Nome", "Digite:", buf, sizeof buf) == 1);
    CHECK(strcmp(buf, "abc") == 0);
    CHECK(g_created_w >= 160 && g_created_h >= 48 && g_destroyed == 1);   // janela do tamanho do conteudo

    reset_script();
    KEY('z'); KEY(27);                                            // Esc cancela
    strcpy(buf, "xy");
    CHECK(ui_prompt("Nome", "Digite:", buf, sizeof buf) == 0);
    CHECK(strcmp(buf, "xy") == 0);                                // buffer intacto

    reset_script();
    KEY('\n');                                                    // foco no 1o botao (OK)
    CHECK(ui_confirm("Sair?", "Tem certeza?") == 1);
    reset_script();
    KEY('\t'); KEY('\n');                                         // Tab -> Cancelar, Enter
    CHECK(ui_confirm("Sair?", "Tem certeza?") == 0);
    reset_script();
    KEY(27);
    CHECK(ui_confirm("Sair?", "Tem certeza?") == 0);
    reset_script();                                               // fechar a janela (X): roteiro vazio
    CHECK(ui_confirm("Sair?", "Tem certeza?") == 0);

    reset_script();
    KEY('\n');
    ui_message("Aviso", "Linha 1\nLinha 2");
    CHECK(g_destroyed == 1);

    reset_script();
    ui_dialog_labels("Sim", "Nao");
    KEY('\t'); KEY(' ');
    CHECK(ui_confirm("Q", "x") == 0);

    reset_script();
    KEY('1'); KEY('2'); KEY('3'); KEY('4'); KEY('5'); KEY('\n');  // buffer de 4 -> 3 caracteres
    char small[4] = "";
    CHECK(ui_prompt("T", "L", small, sizeof small) == 1 && strcmp(small, "123") == 0);
}

int main(void) {
    test_layout();
    test_buttons();
    test_check_radio();
    test_entry();
    test_list();
    test_slider_progress();
    test_focus();
    test_theme_resize_timer();
    test_dialogs();
    printf("\n%d verificacoes, %d falha(s) (%d quadros desenhados)\n", checks, fails, g_frames);
    free(g_frame);
    return fails ? 1 : 0;
}
