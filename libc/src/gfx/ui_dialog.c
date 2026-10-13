// libc/src/gfx/ui_dialog.c — diálogos modais prontos (usam só a API pública de ui.h).
//
// Cada diálogo é um ui_app próprio (outra janela) com laço de eventos aninhado:
// enquanto ele roda, a janela do programa não recebe eventos (ficam na fila).
#include "haos/ui.h"
#include <string.h>

static const char* g_ok     = "OK";
static const char* g_cancel = "Cancelar";

void ui_dialog_labels(const char* ok, const char* cancel) {
    if (ok && ok[0]) g_ok = ok;
    if (cancel && cancel[0]) g_cancel = cancel;
}

typedef struct { ui_app* app; int result; } dlg_t;

static void on_ok(ui_widget* w, void* ctx)     { (void)w; dlg_t* d = (dlg_t*)ctx; d->result = 1; ui_quit(d->app); }
static void on_cancel(ui_widget* w, void* ctx) { (void)w; dlg_t* d = (dlg_t*)ctx; d->result = 0; ui_quit(d->app); }

static int on_key(ui_app* app, int key, void* ctx) {          // Enter = OK, Esc = cancelar
    dlg_t* d = (dlg_t*)ctx;
    if (key == '\n' || key == '\r') { d->result = 1; ui_quit(app); return 1; }
    if (key == 27)                  { d->result = 0; ui_quit(app); return 1; }
    return 0;
}

// mode: 0 = mensagem, 1 = confirmação, 2 = entrada de texto
static int run_dialog(const char* title, const char* text, int mode, char* buf, unsigned long cap) {
    ui_app* app = ui_app_new(title ? title : "", 0, 0);
    if (!app) return 0;
    dlg_t d = { app, 0 };
    ui_widget* root = ui_root(app);

    if (text && text[0]) ui_label(root, text);
    ui_widget* entry = NULL;
    if (mode == 2) {
        entry = ui_entry(root, buf ? buf : "", on_ok, &d);
        if (cap > 1 && cap - 1 < 255) ui_entry_set_max(entry, (int)(cap - 1));
        ui_set_size(entry, 300, 0);
    }
    ui_widget* row = ui_hbox(root, 8);
    ui_spacer(row);
    ui_widget* ok = ui_button(row, g_ok, on_ok, &d);
    ui_set_size(ok, 90, 0);
    if (mode != 0) {
        ui_widget* cancel = ui_button(row, g_cancel, on_cancel, &d);
        ui_set_size(cancel, 90, 0);
    }
    ui_set_key_handler(app, on_key, &d);

    if (ui_run(app) != 0) d.result = 0;                      // não conseguiu abrir a janela
    if (mode == 2 && d.result && buf && cap) {
        strncpy(buf, ui_entry_text(entry), cap - 1);
        buf[cap - 1] = '\0';
    }
    ui_app_free(app);
    return d.result;
}

void ui_message(const char* title, const char* text) { run_dialog(title, text, 0, 0, 0); }
int  ui_confirm(const char* title, const char* text) { return run_dialog(title, text, 1, 0, 0); }
int  ui_prompt(const char* title, const char* label, char* buf, unsigned long cap) {
    return run_dialog(title, label, 2, buf, cap);
}
