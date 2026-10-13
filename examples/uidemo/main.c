// examples/uidemo/main.c — demonstração do toolkit de widgets (haos/ui.h).
//
//   run uidemo.hexec          (ou: Explorador -> /bin/uidemo.hexec)
//
// Mostra: rótulos, botões com callback, caixa de seleção, opções, campo de texto,
// lista (duplo clique), deslizante + barra de progresso, timer e os três diálogos
// modais (mensagem, confirmação e entrada de texto). Troque o tema do sistema com a
// janela aberta: ela se redesenha sozinha.

#include <stdio.h>
#include <string.h>
#include "haos/ui.h"

static ui_widget *g_count_lbl, *g_status, *g_entry, *g_list, *g_progress, *g_spin;
static int g_count;

static void say(const char* s) { ui_label_set_text(g_status, s); }

static void on_inc(ui_widget* w, void* ctx) {
    (void)w; (void)ctx;
    char t[32];
    snprintf(t, sizeof t, "Cliques: %d", ++g_count);
    ui_label_set_text(g_count_lbl, t);
}

static void on_check(ui_widget* w, void* ctx) {
    (void)ctx;
    say(ui_check_get(w) ? "Opcao ligada." : "Opcao desligada.");
}

static void on_radio(ui_widget* w, void* ctx) {
    (void)w;
    char t[48];
    snprintf(t, sizeof t, "Escolhido: %s", (const char*)ctx);
    say(t);
}

static void on_add(ui_widget* w, void* ctx) {
    (void)w; (void)ctx;
    const char* t = ui_entry_text(g_entry);
    if (t[0]) { ui_list_add(g_list, t); ui_entry_set_text(g_entry, ""); say("Item adicionado."); }
    else say("Digite algo no campo primeiro.");
}

static void on_list_activate(ui_widget* w, void* ctx) {
    (void)ctx;
    char t[96];
    const char* it = ui_list_item(w, ui_list_selected(w));
    snprintf(t, sizeof t, "Ativado: %s", it ? it : "?");
    say(t);
}

static void on_slider(ui_widget* w, void* ctx) {
    (void)ctx;
    ui_progress_set(g_progress, ui_slider_get(w));
}

static void on_message(ui_widget* w, void* ctx) {
    (void)w; (void)ctx;
    ui_message("Mensagem", "Isto e um dialogo modal.\nFeche com OK ou Enter.");
}

static void on_confirm(ui_widget* w, void* ctx) {
    (void)w; (void)ctx;
    say(ui_confirm("Confirmar", "Deseja continuar?") ? "Voce confirmou." : "Voce cancelou.");
}

static void on_prompt(ui_widget* w, void* ctx) {
    (void)w; (void)ctx;
    char buf[64];
    strncpy(buf, ui_entry_text(g_entry), sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    if (ui_prompt("Seu nome", "Como voce se chama?", buf, sizeof buf)) {
        char t[96];
        ui_entry_set_text(g_entry, buf);
        snprintf(t, sizeof t, "Ola, %s!", buf);
        say(t);
    }
}

static void on_close(ui_widget* w, void* ctx) { (void)w; ui_quit((ui_app*)ctx); }

static void on_timer(ui_app* app, void* ctx) {
    (void)app; (void)ctx;
    static const char* frames[] = { "|", "/", "-", "\\" };
    static int f;
    ui_label_set_text(g_spin, frames[f = (f + 1) & 3]);
}

int main(void) {
    ui_app* app = ui_app_new("Demo do toolkit", 0, 0);
    if (!app) return 1;
    ui_widget* root = ui_root(app);

    ui_label(root, "Toolkit de widgets do HAOS");
    ui_separator(root);

    ui_widget* row = ui_hbox(root, 8);
    g_count_lbl = ui_label(row, "Cliques: 0");
    ui_set_expand(g_count_lbl, 1);
    g_spin = ui_label(row, "|");
    ui_widget* inc = ui_button(row, "Somar", on_inc, 0);
    ui_set_size(inc, 100, 0);

    ui_check(root, "Uma caixa de selecao", 0, on_check, 0);
    ui_widget* radios = ui_hbox(root, 16);
    ui_widget* r1 = ui_radio(radios, "Verde",    1, on_radio, "Verde");
    ui_radio(radios, "Azul",     1, on_radio, "Azul");
    ui_radio(radios, "Vermelho", 1, on_radio, "Vermelho");
    ui_radio_set(r1);
    ui_separator(root);

    ui_widget* erow = ui_hbox(root, 8);
    g_entry = ui_entry(erow, "", on_add, 0);
    ui_set_expand(g_entry, 1);
    ui_widget* add = ui_button(erow, "Adicionar", on_add, 0);
    ui_set_size(add, 110, 0);

    g_list = ui_list(root, on_list_activate, 0);
    ui_list_set_rows(g_list, 4);
    ui_list_add(g_list, "Primeiro item");
    ui_list_add(g_list, "Segundo item");
    ui_list_add(g_list, "Terceiro item");

    ui_widget* srow = ui_hbox(root, 8);
    ui_label(srow, "Valor:");
    ui_widget* sl = ui_slider(srow, 0, 100, 30, on_slider, 0);
    ui_set_expand(sl, 1);
    g_progress = ui_progress(root);
    ui_progress_set(g_progress, 30);

    ui_widget* drow = ui_hbox(root, 8);
    ui_button(drow, "Mensagem", on_message, 0);
    ui_button(drow, "Confirmar", on_confirm, 0);
    ui_button(drow, "Perguntar nome", on_prompt, 0);

    ui_separator(root);
    g_status = ui_label(root, "Pronto. Experimente os controles acima.");
    ui_widget* brow = ui_hbox(root, 8);
    ui_spacer(brow);
    ui_widget* close = ui_button(brow, "Fechar", on_close, app);
    ui_set_size(close, 100, 0);

    ui_set_timer(app, 150, on_timer, 0);
    int rc = ui_run(app);
    ui_app_free(app);
    return rc == 0 ? 0 : 1;
}
