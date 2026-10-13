#pragma once
#include "types.h"
#include "draw.h"
#include "uitheme.h"

// ============================================================
//  libc/include/haos/ui.h — toolkit de widgets para programas Ring 3
//
//  O programa monta uma ÁRVORE de widgets (caixas verticais/horizontais com
//  layout automático) e liga callbacks; ui_run() cuida do laço de eventos,
//  do foco (Tab), do hover, do tema e do redesenho.
//
//      static void somar(ui_widget* w, void* ctx) {
//          static int n; char t[16]; snprintf(t, sizeof t, "%d", ++n);
//          ui_label_set_text((ui_widget*)ctx, t);
//      }
//      int main(void) {
//          ui_app* app = ui_app_new("Contador", 0, 0);       // 0,0 = ajusta ao conteúdo
//          ui_widget* col = ui_root(app);
//          ui_widget* num = ui_label(col, "0");
//          ui_button(col, "Somar", somar, num);
//          return ui_run(app);
//      }
//
//  - Layout: cada caixa empilha os filhos (vbox) ou os põe lado a lado (hbox).
//    Os filhos preenchem a largura (vbox) / altura (hbox); ui_set_expand() reparte a
//    sobra pelo "peso"; ui_set_size() fixa largura e/ou altura.
//  - Tudo usa as cores do TEMA do sistema e muda sozinho com ele (HAOS_EV_THEME).
//  - Diálogos prontos (ui_message / ui_confirm / ui_prompt) abrem janelas modais.
//
//  Teclado: Tab muda o foco; Enter/Espaço ativam o botão; setas movem listas,
//  opções e o deslizante; o campo de texto aceita edição, setas, Home/End, Delete,
//  Ctrl+C (copia tudo), Ctrl+X (recorta tudo) e Ctrl+V (cola) — o clipboard é o do sistema.
//  Teclas não tratadas pelo widget em foco vão para ui_set_key_handler().
// ============================================================

typedef struct ui_app    ui_app;
typedef struct ui_widget ui_widget;

typedef void (*ui_cb)(ui_widget* w, void* ctx);
typedef int  (*ui_key_cb)(ui_app* app, int key, void* ctx);    // devolva 1 se tratou a tecla
typedef void (*ui_timer_cb)(ui_app* app, void* ctx);

// ---------------- Aplicativo ----------------
// w = h = 0: a janela é criada (em ui_run) do tamanho do conteúdo. Senão, tamanho da
// ÁREA CLIENTE pedido (o kernel pode ajustar). A janela só existe a partir de ui_run().
ui_app*    ui_app_new(const char* title, int w, int h);
void       ui_app_free(ui_app* app);                 // destrói a janela e a árvore
ui_widget* ui_root(ui_app* app);                     // caixa vertical que cobre a janela
int        ui_run(ui_app* app);                      // 0 ao terminar (ui_quit ou janela fechada); -1 se não abriu janela
void       ui_quit(ui_app* app);                     // faz ui_run() retornar
void       ui_redraw(ui_app* app);                   // pede redesenho (as mudanças via ui_* já pedem sozinhas)
void       ui_set_title(ui_app* app, const char* title);
void       ui_set_key_handler(ui_app* app, ui_key_cb cb, void* ctx);
void       ui_set_timer(ui_app* app, unsigned ms, ui_timer_cb cb, void* ctx);   // repetitivo; ms = 0 cancela
int        ui_window(ui_app* app);                   // handle da janela (-1 antes de ui_run)

// ---------------- Contêineres ----------------
ui_widget* ui_vbox(ui_widget* parent, int spacing);  // filhos de cima para baixo
ui_widget* ui_hbox(ui_widget* parent, int spacing);  // filhos da esquerda para a direita
void       ui_set_padding(ui_widget* box, int px);   // margem interna da caixa (a raiz começa com a do tema)

// ---------------- Widgets ----------------
ui_widget* ui_label(ui_widget* parent, const char* text);                  // '\n' quebra linha
void       ui_label_set_text(ui_widget* w, const char* text);

ui_widget* ui_button(ui_widget* parent, const char* text, ui_cb on_click, void* ctx);

ui_widget* ui_check(ui_widget* parent, const char* text, int checked, ui_cb on_change, void* ctx);
int        ui_check_get(ui_widget* w);
void       ui_check_set(ui_widget* w, int checked);

// Opções exclusivas: radios IRMÃOS (mesma caixa) com o mesmo `group` formam um grupo.
ui_widget* ui_radio(ui_widget* parent, const char* text, int group, ui_cb on_select, void* ctx);
int        ui_radio_get(ui_widget* w);               // 1 se é a opção marcada
void       ui_radio_set(ui_widget* w);               // marca esta (e desmarca as outras do grupo)

// Campo de texto de uma linha. on_activate = Enter; ui_on_change() = a cada edição.
ui_widget*   ui_entry(ui_widget* parent, const char* text, ui_cb on_activate, void* ctx);
const char*  ui_entry_text(ui_widget* w);
void         ui_entry_set_text(ui_widget* w, const char* text);
void         ui_entry_set_max(ui_widget* w, int max_chars);   // padrão 255

// Lista com seleção e rolagem. on_activate = duplo clique/Enter; ui_on_change() = muda a seleção.
ui_widget* ui_list(ui_widget* parent, ui_cb on_activate, void* ctx);
int        ui_list_add(ui_widget* w, const char* text);       // devolve o índice (-1 se falhou)
void       ui_list_clear(ui_widget* w);
int        ui_list_count(ui_widget* w);
int        ui_list_selected(ui_widget* w);                    // -1 se nenhuma
void       ui_list_set_selected(ui_widget* w, int index);
const char* ui_list_item(ui_widget* w, int index);
void       ui_list_set_rows(ui_widget* w, int rows);          // linhas visíveis pedidas (padrão 6)

ui_widget* ui_progress(ui_widget* parent);                     // barra 0..100
void       ui_progress_set(ui_widget* w, int percent);

ui_widget* ui_slider(ui_widget* parent, int min, int max, int value, ui_cb on_change, void* ctx);
int        ui_slider_get(ui_widget* w);
void       ui_slider_set(ui_widget* w, int value);

ui_widget* ui_separator(ui_widget* parent);                    // linha fina (horizontal em vbox, vertical em hbox)
ui_widget* ui_spacer(ui_widget* parent);                       // vazio que se expande (empurra os vizinhos)

// ---------------- Propriedades gerais ----------------
void ui_on_change(ui_widget* w, ui_cb cb, void* ctx);          // callback secundário (entry: edição; list: seleção)
void ui_set_enabled(ui_widget* w, int enabled);
void ui_set_visible(ui_widget* w, int visible);
void ui_set_expand(ui_widget* w, int weight);                  // 0 = tamanho natural; >0 = divide a sobra
void ui_set_size(ui_widget* w, int width, int height);         // 0 = automático nesse eixo
void ui_focus(ui_widget* w);
void ui_get_rect(ui_widget* w, int* x, int* y, int* width, int* height);   // posição calculada
ui_app* ui_app_of(ui_widget* w);

// ---------------- Diálogos modais ----------------
void ui_dialog_labels(const char* ok, const char* cancel);     // textos dos botões (padrão "OK"/"Cancelar")
void ui_message(const char* title, const char* text);
int  ui_confirm(const char* title, const char* text);          // 1 = OK, 0 = cancelou/fechou
int  ui_prompt(const char* title, const char* label, char* buf, unsigned long cap);   // 1 = OK (texto em buf)
