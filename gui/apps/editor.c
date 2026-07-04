// gui/apps/editor.c — Bloco de Notas do HAOS
//
// Buffer multi-linha em memória, cursor navegável (setas),
// integração com o VFS para abrir/salvar arquivos, seleção de texto
// (teclado e mouse) e barra de status mostrando o nome do arquivo e
// se há alterações não salvas.
//
// Teclas:
//   Setas          -- move o cursor
//   Shift+Setas    -- estende a seleção de texto
//   Ctrl+A         -- seleciona todo o texto
//   Enter          -- quebra linha (substitui a seleção, se houver)
//   Backspace      -- apaga caractere anterior, ou a seleção inteira
//   Ctrl+S         -- salva no VFS (pede o nome se o arquivo ainda não tiver um)
//   Ctrl+C         -- copia o texto selecionado
//   Ctrl+V         -- cola o conteúdo da área de transferência
//   Ctrl+X         -- recorta o texto selecionado
//   F2             -- também salva no VFS (atalho antigo antes de supporte a Ctrl)
//   ESC            -- fecha o editor (ou cancela o diálogo "salvar como")

#include "editor.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../fs/vfs.h"
#include "../../kernel/lang.h"

#define PAD 8

// Teclas especiais (definidas em keyboard.c)
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_F2    0x84

// Atalhos com Ctrl (definidos em kernel/keyboard.c)
#define KEY_CTRL_S 0x85
#define KEY_CTRL_C 0x86
#define KEY_CTRL_V 0x87
#define KEY_CTRL_X 0x88
#define KEY_CTRL_A 0x89

// Setas com Shift (definidas em kernel/keyboard.c) -- estendem a seleção
#define KEY_SHIFT_UP    0x8A
#define KEY_SHIFT_DOWN  0x8B
#define KEY_SHIFT_LEFT  0x8C
#define KEY_SHIFT_RIGHT 0x8D

// Tamanho máximo da área de transferência: o documento inteiro poderia
// ser copiado, então reservamos espaço para todas as linhas + '\n's.
#define CLIPBOARD_MAX ((EDITOR_COLS + 1) * EDITOR_MAX_LINES)

// Tamanho máximo do nome digitado no diálogo "salvar como"
#define SAVEAS_MAX (VFS_NAME_MAX - 1)

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_COLS + 1];
    int  num_lines;

    int  cursor_row, cursor_col;   // posição do cursor no buffer
    int  scroll_row;               // primeira linha visível

    char filename[VFS_NAME_MAX];   // nome do arquivo (vazio = "sem nome")
    VfsNode* file;                 // nó do VFS associado (NULL = ainda não salvo)
    bool dirty;                    // há alterações não salvas?

    bool     cursor_visible;
    uint64_t last_blink;

    // Mensagem de status temporária (ex: "Salvo!")
    char status_msg[64];
    uint64_t status_until;

    // Área de transferência (clipboard) -- pode conter várias linhas,
    // serializadas com '\n' entre elas (sem '\n' final)
    char clipboard[CLIPBOARD_MAX];
    bool clipboard_has_data;

    // Seleção de texto: âncora fixa + cursor atual é o outro extremo
    bool sel_active;
    int  sel_anchor_row, sel_anchor_col;

    // Diálogo modal "salvar como" (mostrado quando Ctrl+S/F2 é
    // pressionado e o arquivo ainda não tem nome)
    bool saveas_open;
    char saveas_buf[SAVEAS_MAX + 1];
    int  saveas_len;
} EditorState;

// ---- Utilitários internos ------------------------------------

static void editor_set_status(EditorState* e, const char* msg, uint64_t ticks) {
    kstrcpy(e->status_msg, msg);
    e->status_until = ticks + 100;   // ~2s a 50 ticks/s de render, folgado
}

// ---- Seleção de texto -------------------------------------------
// A seleção é representada por uma âncora fixa (onde o usuário começou
// a selecionar) e o cursor atual (o outro extremo, que se move). As
// funções abaixo sempre normalizam para (start <= end) antes de agir.

static void editor_sel_start(EditorState* e) {
    e->sel_active = true;
    e->sel_anchor_row = e->cursor_row;
    e->sel_anchor_col = e->cursor_col;
}

static void editor_sel_clear(EditorState* e) {
    e->sel_active = false;
}

// Preenche (sr,sc)-(er,ec) com o range normalizado da seleção atual.
// Não faz nada (deixa os ponteiros inalterados) se não há seleção.
static void editor_sel_range(EditorState* e, int* sr, int* sc, int* er, int* ec) {
    int ar = e->sel_anchor_row, ac = e->sel_anchor_col;
    int cr = e->cursor_row,     cc = e->cursor_col;
    if (ar < cr || (ar == cr && ac <= cc)) {
        *sr = ar; *sc = ac; *er = cr; *ec = cc;
    } else {
        *sr = cr; *sc = cc; *er = ar; *ec = ac;
    }
}

// Existe uma seleção não-vazia (start != end)?
static bool editor_sel_nonempty(EditorState* e) {
    if (!e->sel_active) return false;
    return e->sel_anchor_row != e->cursor_row || e->sel_anchor_col != e->cursor_col;
}

// Remove o texto selecionado do buffer, juntando as linhas restantes,
// e posiciona o cursor no início de onde a seleção estava.
static void editor_sel_delete(EditorState* e) {
    if (!editor_sel_nonempty(e)) { editor_sel_clear(e); return; }

    int sr, sc, er, ec;
    editor_sel_range(e, &sr, &sc, &er, &ec);

    if (sr == er) {
        // Seleção em uma única linha: desloca o restante para a esquerda
        char* line = e->lines[sr];
        int len = (int)kstrlen(line);
        for (int i = sc; i + (ec - sc) <= len; i++)
            line[i] = line[i + (ec - sc)];
    } else {
        // Junta o começo da primeira linha com o final da última linha
        char* first = e->lines[sr];
        char* last  = e->lines[er];
        first[sc] = 0;
        if ((int)kstrlen(first) + (int)kstrlen(last + ec) <= EDITOR_COLS)
            kstrcat(first, last + ec);

        // Remove as linhas do meio (sr+1 .. er), deslocando as de baixo
        int removed = er - sr;
        for (int i = sr + 1; i + removed < e->num_lines; i++)
            kstrcpy(e->lines[i], e->lines[i + removed]);
        for (int i = e->num_lines - removed; i < e->num_lines; i++)
            if (i >= 0 && i < EDITOR_MAX_LINES) e->lines[i][0] = 0;
        e->num_lines -= removed;
        if (e->num_lines < 1) e->num_lines = 1;
    }

    e->cursor_row = sr;
    e->cursor_col = sc;
    editor_sel_clear(e);
    e->dirty = true;
}

// Carrega o conteúdo de um VfsNode (arquivo) para o buffer de linhas
static void editor_load_from_node(EditorState* e, VfsNode* node) {
    e->num_lines = 1;
    for (int i = 0; i < EDITOR_MAX_LINES; i++) e->lines[i][0] = 0;

    if (!node || !node->data || node->size == 0) return;

    int row = 0, col = 0;
    for (uint32_t i = 0; i < node->size; i++) {
        char c = node->data[i];
        if (c == '\n') {
            if (row < EDITOR_MAX_LINES - 1) row++;
            col = 0;
            if (row >= e->num_lines) e->num_lines = row + 1;
        } else if (col < EDITOR_COLS) {
            e->lines[row][col++] = c;
            e->lines[row][col]   = 0;
            if (row >= e->num_lines) e->num_lines = row + 1;
        }
    }
}

// Serializa o buffer de linhas de volta para uma string única (com \n)
// e escreve no VFS no nó já associado (e->file). Não lida com nomes.
static bool editor_write_to_file(EditorState* e) {
    if (!e->file) return false;

    // Monta buffer temporário concatenando linhas
    // (usa heap, já que o total pode ser grande)
    uint32_t total = 0;
    for (int i = 0; i < e->num_lines; i++)
        total += (uint32_t)kstrlen(e->lines[i]) + 1; // +1 para o '\n'

    char* buf = (char*)kzalloc(total + 1);
    if (!buf) return false;

    uint32_t pos = 0;
    for (int i = 0; i < e->num_lines; i++) {
        size_t len = kstrlen(e->lines[i]);
        kmemcpy(buf + pos, e->lines[i], len);
        pos += (uint32_t)len;
        buf[pos++] = '\n';
    }
    buf[pos] = 0;

    bool ok = vfs_write(e->file, buf);
    kfree(buf);

    if (ok) e->dirty = false;
    return ok;
}

// Salva no arquivo já associado. Se ainda não há nome/arquivo, retorna
// false sem fazer nada -- o chamador (editor_key) deve abrir o diálogo
// "salvar como" nesse caso.
static bool editor_save(EditorState* e) {
    if (!e->filename[0]) return false;   // sem nome definido ainda

    if (!e->file) {
        e->file = vfs_touch(vfs_cwd(), e->filename);
        if (!e->file) return false;
    }
    return editor_write_to_file(e);
}

// Define o nome do arquivo (vindo do diálogo "salvar como"), cria o nó
// no VFS se necessário, e grava o conteúdo atual.
static bool editor_save_as(EditorState* e, Window* win, const char* name) {
    if (!name || !name[0]) return false;

    kstrncpy(e->filename, name, VFS_NAME_MAX - 1);
    e->filename[VFS_NAME_MAX - 1] = 0;

    e->file = vfs_touch(vfs_cwd(), e->filename);
    if (!e->file) return false;

    bool ok = editor_write_to_file(e);

    if (ok && win) {
        char title[96];
        kstrcpy(title, tr(STR_EDITOR_TITLE));
        kstrcat(title, " -- ");
        kstrcat(title, e->filename);
        kstrcpy(win->title, title);
    }
    return ok;
}

// ---- Edição de texto -------------------------------------------

static void editor_insert_char(EditorState* e, char c) {
    if (editor_sel_nonempty(e)) editor_sel_delete(e);
    else editor_sel_clear(e);

    char* line = e->lines[e->cursor_row];
    int len = (int)kstrlen(line);
    if (len >= EDITOR_COLS) return;   // linha cheia

    // Desloca o resto da linha para abrir espaço
    for (int i = len; i > e->cursor_col; i--)
        line[i] = line[i - 1];
    line[e->cursor_col] = c;
    line[len + 1] = 0;
    e->cursor_col++;
    e->dirty = true;
}

static void editor_backspace(EditorState* e) {
    if (editor_sel_nonempty(e)) { editor_sel_delete(e); return; }
    editor_sel_clear(e);

    if (e->cursor_col > 0) {
        char* line = e->lines[e->cursor_row];
        int len = (int)kstrlen(line);
        for (int i = e->cursor_col - 1; i < len; i++)
            line[i] = line[i + 1];
        e->cursor_col--;
        e->dirty = true;
    } else if (e->cursor_row > 0) {
        // Junta com a linha anterior
        char* prev = e->lines[e->cursor_row - 1];
        char* cur  = e->lines[e->cursor_row];
        int prev_len = (int)kstrlen(prev);
        int cur_len  = (int)kstrlen(cur);
        if (prev_len + cur_len <= EDITOR_COLS) {
            kstrcpy(prev + prev_len, cur);
        }
        // Remove a linha atual, deslocando as de baixo
        for (int i = e->cursor_row; i < e->num_lines - 1; i++)
            kstrcpy(e->lines[i], e->lines[i + 1]);
        e->lines[e->num_lines - 1][0] = 0;
        if (e->num_lines > 1) e->num_lines--;
        e->cursor_row--;
        e->cursor_col = prev_len;
        e->dirty = true;
    }
}

static void editor_newline(EditorState* e) {
    if (editor_sel_nonempty(e)) editor_sel_delete(e);
    else editor_sel_clear(e);

    if (e->num_lines >= EDITOR_MAX_LINES) return;   // buffer cheio

    // Desloca todas as linhas abaixo da atual uma posição para baixo
    for (int i = e->num_lines; i > e->cursor_row + 1; i--)
        kstrcpy(e->lines[i], e->lines[i - 1]);

    // A parte da linha atual depois do cursor vira a nova linha
    char* cur = e->lines[e->cursor_row];
    char* next = e->lines[e->cursor_row + 1];
    kstrcpy(next, cur + e->cursor_col);
    cur[e->cursor_col] = 0;

    e->num_lines++;
    e->cursor_row++;
    e->cursor_col = 0;
    e->dirty = true;
}

// ---- Área de transferência (copiar/colar/recortar da seleção) --------

// Serializa o texto selecionado para e->clipboard (linhas unidas por '\n').
// Se não houver seleção, copia a linha inteira onde está o cursor
// (comportamento de conveniência, como em vários editores simples).
static void editor_copy_selection(EditorState* e) {
    int sr, sc, er, ec;

    if (editor_sel_nonempty(e)) {
        editor_sel_range(e, &sr, &sc, &er, &ec);
    } else {
        sr = er = e->cursor_row;
        sc = 0;
        ec = (int)kstrlen(e->lines[e->cursor_row]);
    }

    uint32_t pos = 0;
    for (int row = sr; row <= er && pos < CLIPBOARD_MAX - 1; row++) {
        const char* line = e->lines[row];
        int len = (int)kstrlen(line);
        int start = (row == sr) ? sc : 0;
        int end   = (row == er) ? ec : len;
        if (start < 0) start = 0;
        if (end > len) end = len;

        for (int i = start; i < end && pos < CLIPBOARD_MAX - 1; i++)
            e->clipboard[pos++] = line[i];
        if (row != er && pos < CLIPBOARD_MAX - 1)
            e->clipboard[pos++] = '\n';
    }
    e->clipboard[pos] = 0;
    e->clipboard_has_data = true;
}

static void editor_cut_selection(EditorState* e) {
    editor_copy_selection(e);

    if (editor_sel_nonempty(e)) {
        editor_sel_delete(e);
    } else {
        // Sem seleção: recorta a linha inteira (mesma conveniência do copiar)
        for (int i = e->cursor_row; i < e->num_lines - 1; i++)
            kstrcpy(e->lines[i], e->lines[i + 1]);
        e->lines[e->num_lines - 1][0] = 0;
        if (e->num_lines > 1) {
            e->num_lines--;
            if (e->cursor_row >= e->num_lines) e->cursor_row = e->num_lines - 1;
        } else {
            e->lines[0][0] = 0;
        }
        e->cursor_col = 0;
    }
    e->dirty = true;
}

// Insere o conteúdo do clipboard na posição do cursor, quebrando linhas
// onde houver '\n'. Substitui a seleção atual, se houver.
static void editor_paste_clipboard(EditorState* e) {
    if (!e->clipboard_has_data) return;
    if (editor_sel_nonempty(e)) editor_sel_delete(e);
    else editor_sel_clear(e);

    for (uint32_t i = 0; e->clipboard[i]; i++) {
        char ch = e->clipboard[i];
        if (ch == '\n') editor_newline(e);
        else if (ch >= 0x20 && ch < 0x7F) editor_insert_char(e, ch);
        // Outros caracteres de controle são ignorados
    }
    e->dirty = true;
}

// Move o cursor de acordo com a tecla de seta base (KEY_UP/DOWN/LEFT/RIGHT).
static void editor_move_cursor(EditorState* e, uint8_t key) {
    int len = (int)kstrlen(e->lines[e->cursor_row]);
    if (key == KEY_LEFT) {
        if (e->cursor_col > 0) e->cursor_col--;
        else if (e->cursor_row > 0) {
            e->cursor_row--;
            e->cursor_col = (int)kstrlen(e->lines[e->cursor_row]);
        }
    } else if (key == KEY_RIGHT) {
        if (e->cursor_col < len) e->cursor_col++;
        else if (e->cursor_row < e->num_lines - 1) {
            e->cursor_row++;
            e->cursor_col = 0;
        }
    } else if (key == KEY_UP) {
        if (e->cursor_row > 0) {
            e->cursor_row--;
            int nlen = (int)kstrlen(e->lines[e->cursor_row]);
            if (e->cursor_col > nlen) e->cursor_col = nlen;
        }
    } else if (key == KEY_DOWN) {
        if (e->cursor_row < e->num_lines - 1) {
            e->cursor_row++;
            int nlen = (int)kstrlen(e->lines[e->cursor_row]);
            if (e->cursor_col > nlen) e->cursor_col = nlen;
        }
    }
}

// Trata uma tecla de seta simples: cancela seleção e move o cursor.
static void editor_arrow_key(EditorState* e, uint8_t key) {
    editor_sel_clear(e);
    editor_move_cursor(e, key);
}

// Trata Shift+Seta: garante que há uma âncora de seleção (criando-a na
// posição atual se ainda não houver), move o cursor, e mantém a seleção
// ativa entre a âncora e a nova posição do cursor.
static void editor_shift_arrow_key(EditorState* e, uint8_t base_key) {
    if (!e->sel_active) editor_sel_start(e);
    editor_move_cursor(e, base_key);
    // sel_active permanece true; a âncora não muda, só o cursor se moveu
}

// Ctrl+A: seleciona todo o conteúdo do documento.
static void editor_select_all(EditorState* e) {
    e->sel_active     = true;
    e->sel_anchor_row  = 0;
    e->sel_anchor_col  = 0;
    e->cursor_row = e->num_lines - 1;
    e->cursor_col = (int)kstrlen(e->lines[e->cursor_row]);
}

// ---- Layout geométrico compartilhado (desenho e mouse) -----------
// Calcula a área de texto em coordenadas absolutas de tela, a mesma
// usada tanto por editor_draw quanto pela conversão de clique do mouse
// em posição (linha, coluna) no buffer.
typedef struct {
    int bx, by;        // canto superior esquerdo da área de texto
    int bw, bh;         // largura/altura total da área de conteúdo
    int text_h;         // altura da área de texto (sem a barra de status)
    int visible_rows;   // linhas visíveis simultaneamente
} EditorLayout;

static void editor_compute_layout(Window* win, EditorLayout* lo) {
    lo->bx = win->x + WIN_BORDER + PAD;
    lo->by = win->y + WIN_BORDER + TITLE_BAR_H + 1 + PAD;
    lo->bw = (int)win->w - WIN_BORDER*2 - PAD*2;
    lo->bh = (int)win->h - WIN_BORDER - TITLE_BAR_H - 1 - PAD*2;

    int status_h = FONT_H + 6;
    lo->text_h = lo->bh - status_h;
    lo->visible_rows = lo->text_h / FONT_H;
    if (lo->visible_rows < 1) lo->visible_rows = 1;
}

// Converte uma posição de tela (cx, cy) na posição (row, col) mais
// próxima no buffer de texto, considerando o scroll atual.
static void editor_screen_to_pos(EditorState* e, Window* win,
                                  int32_t cx, int32_t cy, int* out_row, int* out_col) {
    EditorLayout lo;
    editor_compute_layout(win, &lo);

    int rel_y = (int)cy - lo.by;
    int row = e->scroll_row + (rel_y >= 0 ? rel_y / FONT_H : 0);
    if (row < 0) row = 0;
    if (row >= e->num_lines) row = e->num_lines - 1;

    int rel_x = (int)cx - (lo.bx + FONT_W * 5);
    int col = (rel_x >= 0) ? (rel_x + FONT_W/2) / FONT_W : 0;   // arredonda pro caractere mais próximo
    int len = (int)kstrlen(e->lines[row]);
    if (col < 0) col = 0;
    if (col > len) col = len;

    *out_row = row;
    *out_col = col;
}

// ---- Mouse: clique posiciona o cursor, arraste seleciona texto ----

static void editor_on_click(Window* win, int32_t cx, int32_t cy) {
    EditorState* e = (EditorState*)win->content;
    if (!e || e->saveas_open) return;   // diálogo modal aberto: ignora cliques no texto

    int row, col;
    editor_screen_to_pos(e, win, cx, cy, &row, &col);
    e->cursor_row = row;
    e->cursor_col = col;
    editor_sel_start(e);   // âncora aqui; se o usuário arrastar, vira seleção
}

static void editor_on_drag(Window* win, int32_t cx, int32_t cy) {
    EditorState* e = (EditorState*)win->content;
    if (!e || e->saveas_open) return;

    int row, col;
    editor_screen_to_pos(e, win, cx, cy, &row, &col);
    e->cursor_row = row;
    e->cursor_col = col;
    // sel_active já foi ligado pelo on_click; se o cursor voltou exatamente
    // à âncora, editor_sel_nonempty() naturalmente trata como "sem seleção"
}

static void editor_on_mouse_up(Window* win) {
    EditorState* e = (EditorState*)win->content;
    if (!e) return;
    // Se não houve arraste (âncora == cursor), não há seleção real;
    // mantemos sel_active=true mas editor_sel_nonempty() já retorna false
    // nesse caso, então não é preciso fazer nada aqui.
    (void)e;
}

// ---- Renderização -----------------------------------------------

static void editor_draw(Window* win) {
    EditorState* e = (EditorState*)win->content;
    if (!e) return;

    EditorLayout lo;
    editor_compute_layout(win, &lo);
    int bx = lo.bx, by = lo.by, bw = lo.bw;
    int text_h = lo.text_h, visible_rows = lo.visible_rows;

    // Ajusta scroll para manter o cursor visível
    if (e->cursor_row < e->scroll_row) e->scroll_row = e->cursor_row;
    if (e->cursor_row >= e->scroll_row + visible_rows)
        e->scroll_row = e->cursor_row - visible_rows + 1;
    if (e->scroll_row < 0) e->scroll_row = 0;

    // Fundo da área de texto
    fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)text_h, COLOR_TERM_BG);

    // Range normalizado da seleção (se houver), calculado uma vez
    bool has_sel = editor_sel_nonempty(e);
    int sr = 0, sc = 0, er = 0, ec = 0;
    if (has_sel) editor_sel_range(e, &sr, &sc, &er, &ec);

    for (int r = 0; r < visible_rows; r++) {
        int line_idx = e->scroll_row + r;
        int ry = by + r * FONT_H;
        if (line_idx >= e->num_lines) continue;

        int line_len = (int)kstrlen(e->lines[line_idx]);

        // Destaque de seleção (desenhado atrás do texto)
        if (has_sel && line_idx >= sr && line_idx <= er) {
            int hl_start = (line_idx == sr) ? sc : 0;
            int hl_end   = (line_idx == er) ? ec : line_len;
            // Linhas que continuam na seleção para a próxima (não a última)
            // ganham um caractere extra de destaque, sugerindo a quebra de linha
            if (line_idx != er) hl_end = line_len + 1;
            if (hl_end > hl_start) {
                int hx = bx + FONT_W * (5 + hl_start);
                int hw = FONT_W * (hl_end - hl_start);
                fb_fill_rect((uint32_t)hx, (uint32_t)ry, (uint32_t)hw, FONT_H, COLOR_SELECTED);
            }
        }

        // Número da linha (sutil, estilo editor de código)
        char lineno[8];
        kuitoa((uint64_t)(line_idx + 1), lineno);
        fb_draw_string((uint32_t)bx, (uint32_t)ry, lineno, 0x3A5580, 0, true);

        fb_draw_string((uint32_t)(bx + FONT_W * 5), (uint32_t)ry,
                       e->lines[line_idx], COLOR_TERM_FG, 0, true);

        // Cursor nesta linha?
        if (line_idx == e->cursor_row && e->cursor_visible) {
            int cx = bx + FONT_W * (5 + e->cursor_col);
            fb_fill_rect((uint32_t)cx, (uint32_t)(ry + 1), 2, FONT_H - 3, COLOR_ACCENT);
        }
    }

    // Barra de status
    int status_h = FONT_H + 6;
    int sy = by + text_h + 3;
    fb_fill_rect((uint32_t)bx, (uint32_t)sy, (uint32_t)bw, (uint32_t)(status_h - 3), 0x0E1830);
    fb_fill_rect((uint32_t)bx, (uint32_t)sy, (uint32_t)bw, 1, 0x223A66);

    char status[128];
    kstrcpy(status, e->filename[0] ? e->filename : "(sem nome)");
    if (e->dirty) kstrcat(status, " *");
    kstrcat(status, "   [Ctrl+S] Salvar   [ESC] Fechar");
    fb_draw_string((uint32_t)(bx + 4), (uint32_t)(sy + 3), status, COLOR_TEXT_DIM, 0, true);

    if (e->status_msg[0]) {
        uint32_t tw = fb_text_width(e->status_msg);
        fb_draw_string((uint32_t)(bx + bw - (int32_t)tw - 4), (uint32_t)(sy + 3),
                       e->status_msg, COLOR_ACCENT, 0, true);
    }

    // ---- Diálogo modal "salvar como" (desenhado por cima de tudo) ----
    if (e->saveas_open) {
        // Véu escurecendo o conteúdo por trás do diálogo
        fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)(text_h + 6 + FONT_H), 0x000000);

        int mw = 320, mh = 90;
        if (mw > bw - 20) mw = bw - 20;
        int mx = bx + (bw - mw) / 2;
        int my = by + (text_h - mh) / 2;
        if (my < by) my = by;

        fb_fill_rect((uint32_t)mx, (uint32_t)my, (uint32_t)mw, (uint32_t)mh, 0x101830);
        fb_draw_rect((uint32_t)mx, (uint32_t)my, (uint32_t)mw, (uint32_t)mh, COLOR_ACCENT, 1);

        fb_draw_string((uint32_t)(mx + 12), (uint32_t)(my + 10),
                       tr(STR_EDITOR_SAVEAS_PROMPT), COLOR_TEXT_LIGHT, 0, true);

        int fx = mx + 12, fy = my + 34;
        int fw = mw - 24;
        fb_fill_rect((uint32_t)fx, (uint32_t)fy, (uint32_t)fw, FONT_H + 6, COLOR_TERM_BG);
        fb_draw_rect((uint32_t)fx, (uint32_t)fy, (uint32_t)fw, FONT_H + 6, 0x2A5AAA, 1);
        fb_draw_string((uint32_t)(fx + 4), (uint32_t)(fy + 3), e->saveas_buf, COLOR_TERM_FG, 0, true);

        // Cursor piscante no fim do texto digitado
        if (e->cursor_visible) {
            int ccx = fx + 4 + FONT_W * e->saveas_len;
            fb_fill_rect((uint32_t)ccx, (uint32_t)(fy + 3), 2, FONT_H - 3, COLOR_ACCENT);
        }

        fb_draw_string((uint32_t)(mx + 12), (uint32_t)(my + mh - 20),
                       tr(STR_EDITOR_SAVEAS_HINT), COLOR_TEXT_DIM, 0, true);
    }
}

// ---- Teclado ------------------------------------------------------

// Abre o diálogo "salvar como", pré-preenchendo com o nome atual (se houver).
static void editor_open_saveas(EditorState* e) {
    e->saveas_open = true;
    kstrncpy(e->saveas_buf, e->filename, SAVEAS_MAX);
    e->saveas_buf[SAVEAS_MAX] = 0;
    e->saveas_len = (int)kstrlen(e->saveas_buf);
}

// Trata teclas enquanto o diálogo "salvar como" está aberto.
// Retorna depois de consumir a tecla (o diálogo bloqueia o editor por trás).
static void editor_saveas_key(EditorState* e, Window* win, uint8_t c) {
    if (c == 27) {                 // ESC cancela o diálogo
        e->saveas_open = false;
        return;
    }
    if (c == '\n' || c == '\r') {   // Enter confirma
        e->saveas_open = false;
        if (e->saveas_len == 0) {
            editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
            return;
        }
        if (editor_save_as(e, win, e->saveas_buf))
            editor_set_status(e, tr(STR_EDITOR_SAVED), 0);
        else
            editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
        return;
    }
    if (c == '\b') {                // Backspace apaga o último caractere digitado
        if (e->saveas_len > 0) {
            e->saveas_len--;
            e->saveas_buf[e->saveas_len] = 0;
        }
        return;
    }
    // Caracteres imprimíveis (nomes de arquivo simples: sem espaços/barras
    // seria o ideal, mas não restringimos aqui para manter simples)
    if (c >= 0x20 && c < 0x7F && e->saveas_len < SAVEAS_MAX) {
        e->saveas_buf[e->saveas_len++] = (char)c;
        e->saveas_buf[e->saveas_len] = 0;
    }
}

static void editor_key(Window* win, uint8_t c) {
    EditorState* e = (EditorState*)win->content;
    if (!e) return;

    if (e->saveas_open) {
        editor_saveas_key(e, win, c);
        return;
    }

    if (c == 27) {           // ESC
        wm_close(win);
        return;
    }
    if (c == KEY_F2 || c == KEY_CTRL_S) {   // Salvar (F2 ou Ctrl+S)
        if (!e->filename[0]) {
            // Sem nome ainda: pede ao usuário antes de salvar
            editor_open_saveas(e);
            return;
        }
        if (editor_save(e))
            editor_set_status(e, tr(STR_EDITOR_SAVED), 0);
        else
            editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
        return;
    }
    if (c == KEY_CTRL_C) {   // Copiar seleção (ou linha atual, se não houver)
        editor_copy_selection(e);
        editor_set_status(e, tr(STR_EDITOR_COPIED), 0);
        return;
    }
    if (c == KEY_CTRL_X) {   // Recortar seleção (ou linha atual)
        editor_cut_selection(e);
        editor_set_status(e, tr(STR_EDITOR_CUT), 0);
        return;
    }
    if (c == KEY_CTRL_V) {   // Colar
        editor_paste_clipboard(e);
        editor_set_status(e, tr(STR_EDITOR_PASTED), 0);
        return;
    }
    if (c == KEY_CTRL_A) {   // Selecionar tudo
        editor_select_all(e);
        return;
    }
    if (c == KEY_SHIFT_UP)    { editor_shift_arrow_key(e, KEY_UP);    return; }
    if (c == KEY_SHIFT_DOWN)  { editor_shift_arrow_key(e, KEY_DOWN);  return; }
    if (c == KEY_SHIFT_LEFT)  { editor_shift_arrow_key(e, KEY_LEFT);  return; }
    if (c == KEY_SHIFT_RIGHT) { editor_shift_arrow_key(e, KEY_RIGHT); return; }
    if (c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT) {
        editor_arrow_key(e, c);
        return;
    }
    if (c == '\n' || c == '\r') {
        editor_newline(e);
        return;
    }
    if (c == '\b') {
        editor_backspace(e);
        return;
    }
    if (c >= 0x20 && c < 0x7F) {
        editor_insert_char(e, (char)c);
    }
}

// ---- Registro global de janelas de editor abertas ----------------
// Permite que o loop principal do desktop atualize o cursor piscante
// de todas as janelas de editor com uma única chamada, independente
// de onde a janela foi criada (ícone do desktop ou comando 'edit').
#define MAX_EDITOR_INSTANCES 8
static Window* g_editor_wins[MAX_EDITOR_INSTANCES];
static int     g_editor_win_count = 0;

static void editor_register(Window* w) {
    if (!w) return;
    for (int i = 0; i < g_editor_win_count; i++)
        if (g_editor_wins[i] == w) return;
    if (g_editor_win_count < MAX_EDITOR_INSTANCES)
        g_editor_wins[g_editor_win_count++] = w;
}

void editor_tick_all(uint64_t ticks) {
    for (int i = 0; i < g_editor_win_count; ) {
        if (g_editor_wins[i] && g_editor_wins[i]->active) {
            editor_tick(g_editor_wins[i], ticks);
            i++;
        } else {
            for (int j = i; j < g_editor_win_count - 1; j++)
                g_editor_wins[j] = g_editor_wins[j + 1];
            g_editor_win_count--;
        }
    }
}

// ---- Criação --------------------------------------------------------

Window* editor_create(int32_t x, int32_t y, const char* path) {
    uint32_t w = FONT_W * (EDITOR_COLS + 5) + (uint32_t)(WIN_BORDER*2 + PAD*2);
    uint32_t h = FONT_H * (EDITOR_ROWS + 2) + (uint32_t)(WIN_BORDER + TITLE_BAR_H + 1 + PAD*2);

    Window* win = wm_create(WIN_TYPE_TERMINAL, x, y, w, h, tr(STR_EDITOR_TITLE));
    if (!win) return NULL;

    EditorState* e = (EditorState*)kzalloc(sizeof(EditorState));
    if (!e) return win;

    e->num_lines      = 1;
    e->cursor_visible  = true;
    e->last_blink      = 0;
    e->status_msg[0]   = 0;
    e->status_until    = 0;
    e->clipboard[0]         = 0;
    e->clipboard_has_data   = false;
    e->sel_active      = false;
    e->sel_anchor_row  = 0;
    e->sel_anchor_col  = 0;
    e->saveas_open     = false;
    e->saveas_buf[0]   = 0;
    e->saveas_len      = 0;

    if (path && path[0]) {
        kstrcpy(e->filename, path);
        VfsNode* node = vfs_resolve(vfs_cwd(), path);
        if (node && node->type == VFS_FILE) {
            e->file = node;
            editor_load_from_node(e, node);
        }
        // título com o nome do arquivo
        char title[96];
        kstrcpy(title, tr(STR_EDITOR_TITLE));
        kstrcat(title, " -- ");
        kstrcat(title, path);
        kstrcpy(win->title, title);
    }

    win->content      = e;
    win->draw_content = editor_draw;
    win->on_key       = editor_key;
    win->on_click     = editor_on_click;
    win->on_drag      = editor_on_drag;
    win->on_mouse_up  = editor_on_mouse_up;
    editor_register(win);
    return win;
}

void editor_tick(Window* win, uint64_t ticks) {
    if (!win || !win->content) return;
    EditorState* e = (EditorState*)win->content;
    if (ticks - e->last_blink > 50) {
        e->cursor_visible = !e->cursor_visible;
        e->last_blink = ticks;
    }
    if (e->status_msg[0] && ticks > e->status_until) {
        e->status_msg[0] = 0;
    }
}
