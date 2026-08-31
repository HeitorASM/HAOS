#include "editor.h"
#include "../wm.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../kernel/keyboard.h"
#include "../../fs/vfs.h"
#include "../../kernel/lang.h"

#define PAD 8

#define CLIPBOARD_MAX ((EDITOR_COLS + 1) * EDITOR_MAX_LINES)
#define SAVEAS_MAX (VFS_NAME_MAX - 1)

struct EditorState {
    char lines[EDITOR_MAX_LINES][EDITOR_COLS + 1];
    int  num_lines;

    int  cursor_row, cursor_col;
    int  scroll_row;

    char filename[VFS_NAME_MAX];
    VfsNode* file;
    bool dirty;

    bool     cursor_visible;
    uint64_t last_blink;

    char status_msg[64];
    uint64_t status_until;

    char clipboard[CLIPBOARD_MAX];
    bool clipboard_has_data;

    bool sel_active;
    int  sel_anchor_row, sel_anchor_col;

    bool saveas_open;
    char saveas_buf[SAVEAS_MAX + 1];
    int  saveas_len;
};

static void editor_set_status(EditorState* e, const char* msg, uint64_t ticks) {
    kstrcpy(e->status_msg, msg);
    e->status_until = ticks + 100;
}

static void editor_sel_start(EditorState* e) {
    e->sel_active = true;
    e->sel_anchor_row = e->cursor_row;
    e->sel_anchor_col = e->cursor_col;
}

static void editor_sel_clear(EditorState* e) {
    e->sel_active = false;
}

static void editor_sel_range(EditorState* e, int* sr, int* sc, int* er, int* ec) {
    int ar = e->sel_anchor_row, ac = e->sel_anchor_col;
    int cr = e->cursor_row,     cc = e->cursor_col;
    if (ar < cr || (ar == cr && ac <= cc)) {
        *sr = ar; *sc = ac; *er = cr; *ec = cc;
    } else {
        *sr = cr; *sc = cc; *er = ar; *ec = ac;
    }
}

static bool editor_sel_nonempty(EditorState* e) {
    if (!e->sel_active) return false;
    return e->sel_anchor_row != e->cursor_row || e->sel_anchor_col != e->cursor_col;
}

static void editor_sel_delete(EditorState* e) {
    if (!editor_sel_nonempty(e)) { editor_sel_clear(e); return; }

    int sr, sc, er, ec;
    editor_sel_range(e, &sr, &sc, &er, &ec);

    if (sr == er) {
        char* line = e->lines[sr];
        int len = (int)kstrlen(line);
        for (int i = sc; i + (ec - sc) <= len; i++)
            line[i] = line[i + (ec - sc)];
    } else {
        char* first = e->lines[sr];
        char* last  = e->lines[er];
        first[sc] = 0;
        if ((int)kstrlen(first) + (int)kstrlen(last + ec) <= EDITOR_COLS)
            kstrcat(first, last + ec);

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

static bool editor_write_to_file(EditorState* e) {
    if (!e->file) return false;

    uint32_t total = 0;
    for (int i = 0; i < e->num_lines; i++)
        total += (uint32_t)kstrlen(e->lines[i]) + 1;

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

static bool editor_save(EditorState* e) {
    if (!e->filename[0]) return false;

    if (!e->file) {
        e->file = vfs_touch(vfs_cwd(), e->filename);
        if (!e->file) return false;
    }
    return editor_write_to_file(e);
}

// forward decl (EditorWindow definida abaixo, mas usada aqui)
class EditorWindow;
static bool editor_save_as(EditorState* e, EditorWindow* win, const char* name);

static void editor_insert_char(EditorState* e, char c) {
    if (editor_sel_nonempty(e)) editor_sel_delete(e);
    else editor_sel_clear(e);

    char* line = e->lines[e->cursor_row];
    int len = (int)kstrlen(line);
    if (len >= EDITOR_COLS) return;

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
        char* prev = e->lines[e->cursor_row - 1];
        char* cur  = e->lines[e->cursor_row];
        int prev_len = (int)kstrlen(prev);
        int cur_len  = (int)kstrlen(cur);
        if (prev_len + cur_len <= EDITOR_COLS) {
            kstrcpy(prev + prev_len, cur);
        }
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

    if (e->num_lines >= EDITOR_MAX_LINES) return;

    for (int i = e->num_lines; i > e->cursor_row + 1; i--)
        kstrcpy(e->lines[i], e->lines[i - 1]);

    char* cur = e->lines[e->cursor_row];
    char* next = e->lines[e->cursor_row + 1];
    kstrcpy(next, cur + e->cursor_col);
    cur[e->cursor_col] = 0;

    e->num_lines++;
    e->cursor_row++;
    e->cursor_col = 0;
    e->dirty = true;
}

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

static void editor_paste_clipboard(EditorState* e) {
    if (!e->clipboard_has_data) return;
    if (editor_sel_nonempty(e)) editor_sel_delete(e);
    else editor_sel_clear(e);

    for (uint32_t i = 0; e->clipboard[i]; i++) {
        char ch = e->clipboard[i];
        if (ch == '\n') editor_newline(e);
        else if (ch >= 0x20 && ch < 0x7F) editor_insert_char(e, ch);
    }
    e->dirty = true;
}

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

static void editor_arrow_key(EditorState* e, uint8_t key) {
    editor_sel_clear(e);
    editor_move_cursor(e, key);
}

static void editor_shift_arrow_key(EditorState* e, uint8_t base_key) {
    if (!e->sel_active) editor_sel_start(e);
    editor_move_cursor(e, base_key);
}

static void editor_select_all(EditorState* e) {
    e->sel_active     = true;
    e->sel_anchor_row  = 0;
    e->sel_anchor_col  = 0;
    e->cursor_row = e->num_lines - 1;
    e->cursor_col = (int)kstrlen(e->lines[e->cursor_row]);
}

struct EditorLayout {
    int bx, by;
    int bw, bh;
    int text_h;
    int visible_rows;
};

// ============================================================
//  EditorWindow — subclasse de Window. Todo o layout geométrico
//  (editor_compute_layout) agora usa content_area_absolute() em
//  vez de win->x/y/w/h manualmente, e os 4 callbacks antigos
//  (on_click/on_drag/on_mouse_up/on_key) viram um único on_event().
// ============================================================
class EditorWindow : public Window {
public:
    EditorState m_state;

    EditorWindow(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title)
        : Window(x, y, w, h, title, WinType::Terminal)
    {
        kmemset(&m_state, 0, sizeof(EditorState));
        EditorState* e = &m_state;
        e->num_lines      = 1;
        e->cursor_visible = true;
        e->last_blink     = 0;
        e->status_msg[0]  = 0;
        e->status_until   = 0;
        e->clipboard[0]        = 0;
        e->clipboard_has_data  = false;
        e->sel_active     = false;
        e->sel_anchor_row = 0;
        e->sel_anchor_col = 0;
        e->saveas_open    = false;
        e->saveas_buf[0]  = 0;
        e->saveas_len     = 0;
    }

    EditorLayout compute_layout() const {
        EditorLayout lo;
        Rect content = content_area_absolute();
        lo.bx = content.x + PAD;
        lo.by = content.y + PAD;
        lo.bw = (int)content.w - PAD * 2;
        lo.bh = (int)content.h - PAD * 2;

        int status_h = FONT_H + 6;
        lo.text_h = lo.bh - status_h;
        lo.visible_rows = lo.text_h / FONT_H;
        if (lo.visible_rows < 1) lo.visible_rows = 1;
        return lo;
    }

    void screen_to_pos(int32_t cx, int32_t cy, int* out_row, int* out_col) const {
        EditorState* e = const_cast<EditorState*>(&m_state);
        EditorLayout lo = compute_layout();

        int rel_y = (int)cy - lo.by;
        int row = e->scroll_row + (rel_y >= 0 ? rel_y / FONT_H : 0);
        if (row < 0) row = 0;
        if (row >= e->num_lines) row = e->num_lines - 1;

        int rel_x = (int)cx - (lo.bx + FONT_W * 5);
        int col = (rel_x >= 0) ? (rel_x + FONT_W/2) / FONT_W : 0;
        int len = (int)kstrlen(e->lines[row]);
        if (col < 0) col = 0;
        if (col > len) col = len;

        *out_row = row;
        *out_col = col;
    }

    void draw(int32_t ox, int32_t oy) override {
        Window::draw(ox, oy);
        if (!active || minimized) return;

        EditorState* e = &m_state;
        EditorLayout lo = compute_layout();
        int bx = lo.bx, by = lo.by, bw = lo.bw;
        int text_h = lo.text_h, visible_rows = lo.visible_rows;

        if (e->cursor_row < e->scroll_row) e->scroll_row = e->cursor_row;
        if (e->cursor_row >= e->scroll_row + visible_rows)
            e->scroll_row = e->cursor_row - visible_rows + 1;
        if (e->scroll_row < 0) e->scroll_row = 0;

        fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bw, (uint32_t)text_h, COLOR_TERM_BG);

        bool has_sel = editor_sel_nonempty(e);
        int sr = 0, sc = 0, er = 0, ec = 0;
        if (has_sel) editor_sel_range(e, &sr, &sc, &er, &ec);

        for (int r = 0; r < visible_rows; r++) {
            int line_idx = e->scroll_row + r;
            int ry = by + r * FONT_H;
            if (line_idx >= e->num_lines) continue;

            int line_len = (int)kstrlen(e->lines[line_idx]);

            if (has_sel && line_idx >= sr && line_idx <= er) {
                int hl_start = (line_idx == sr) ? sc : 0;
                int hl_end   = (line_idx == er) ? ec : line_len;
                if (line_idx != er) hl_end = line_len + 1;
                if (hl_end > hl_start) {
                    int hx = bx + FONT_W * (5 + hl_start);
                    int hw = FONT_W * (hl_end - hl_start);
                    fb_fill_rect((uint32_t)hx, (uint32_t)ry, (uint32_t)hw, FONT_H, COLOR_SELECTED);
                }
            }

            char lineno[8];
            kuitoa((uint64_t)(line_idx + 1), lineno);
            fb_draw_string((uint32_t)bx, (uint32_t)ry, lineno, 0x3A5580, 0, true);

            fb_draw_string((uint32_t)(bx + FONT_W * 5), (uint32_t)ry,
                           e->lines[line_idx], COLOR_TERM_FG, 0, true);

            if (line_idx == e->cursor_row && e->cursor_visible) {
                int ccx = bx + FONT_W * (5 + e->cursor_col);
                fb_fill_rect((uint32_t)ccx, (uint32_t)(ry + 1), 2, FONT_H - 3, COLOR_ACCENT);
            }
        }

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

        if (e->saveas_open) {
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

            if (e->cursor_visible) {
                int ccx = fx + 4 + FONT_W * e->saveas_len;
                fb_fill_rect((uint32_t)ccx, (uint32_t)(fy + 3), 2, FONT_H - 3, COLOR_ACCENT);
            }

            fb_draw_string((uint32_t)(mx + 12), (uint32_t)(my + mh - 20),
                           tr(STR_EDITOR_SAVEAS_HINT), COLOR_TEXT_DIM, 0, true);
        }
    }

    void open_saveas() {
        EditorState* e = &m_state;
        e->saveas_open = true;
        kstrncpy(e->saveas_buf, e->filename, SAVEAS_MAX);
        e->saveas_buf[SAVEAS_MAX] = 0;
        e->saveas_len = (int)kstrlen(e->saveas_buf);
    }

    void saveas_key(uint8_t c) {
        EditorState* e = &m_state;
        if (c == 27) {
            e->saveas_open = false;
            return;
        }
        if (c == '\n' || c == '\r') {
            e->saveas_open = false;
            if (e->saveas_len == 0) {
                editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
                return;
            }
            if (editor_save_as(e, this, e->saveas_buf))
                editor_set_status(e, tr(STR_EDITOR_SAVED), 0);
            else
                editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
            return;
        }
        if (c == '\b' || c == 0x08) {
            if (e->saveas_len > 0) {
                e->saveas_len--;
                e->saveas_buf[e->saveas_len] = 0;
            }
            return;
        }
        if (c >= 0x20 && c < 0x7F && e->saveas_len < SAVEAS_MAX) {
            e->saveas_buf[e->saveas_len++] = (char)c;
            e->saveas_buf[e->saveas_len] = 0;
        }
    }

    EventResult on_event(const WidgetEvent& ev) override {
        EditorState* e = &m_state;

        if (ev.type == EventType::MouseDown) {
            if (e->saveas_open) return EventResult::Handled;
            int row, col;
            screen_to_pos(ev.x, ev.y, &row, &col);
            e->cursor_row = row;
            e->cursor_col = col;
            editor_sel_start(e);
            return EventResult::Handled;
        }

        if (ev.type == EventType::MouseDrag) {
            if (e->saveas_open) return EventResult::Handled;
            int row, col;
            screen_to_pos(ev.x, ev.y, &row, &col);
            e->cursor_row = row;
            e->cursor_col = col;
            return EventResult::Handled;
        }

        if (ev.type == EventType::MouseUp) {
            // Sem ação extra necessária — ver comentário na versão
            // anterior (editor_on_mouse_up): sel_active permanece
            // true, mas editor_sel_nonempty() já trata corretamente
            // o caso de "clique sem arrastar".
            return EventResult::Handled;
        }

        if (ev.type == EventType::KeyDown) {
            uint8_t c = ev.key;

            if (e->saveas_open) {
                saveas_key(c);
                return EventResult::Handled;
            }

            if (c == 27) { wm_close(this); return EventResult::Handled; }
            if (c == KEY_F2 || c == KEY_CTRL_S) {
                if (!e->filename[0]) { open_saveas(); return EventResult::Handled; }
                if (editor_save(e)) editor_set_status(e, tr(STR_EDITOR_SAVED), 0);
                else editor_set_status(e, tr(STR_EDITOR_SAVE_FAILED), 0);
                return EventResult::Handled;
            }
            if (c == KEY_CTRL_C) {
                editor_copy_selection(e);
                editor_set_status(e, tr(STR_EDITOR_COPIED), 0);
                return EventResult::Handled;
            }
            if (c == KEY_CTRL_X) {
                editor_cut_selection(e);
                editor_set_status(e, tr(STR_EDITOR_CUT), 0);
                return EventResult::Handled;
            }
            if (c == KEY_CTRL_V) {
                editor_paste_clipboard(e);
                editor_set_status(e, tr(STR_EDITOR_PASTED), 0);
                return EventResult::Handled;
            }
            if (c == KEY_CTRL_A) { editor_select_all(e); return EventResult::Handled; }
            if (c == KEY_SHIFT_UP)    { editor_shift_arrow_key(e, KEY_UP);    return EventResult::Handled; }
            if (c == KEY_SHIFT_DOWN)  { editor_shift_arrow_key(e, KEY_DOWN);  return EventResult::Handled; }
            if (c == KEY_SHIFT_LEFT)  { editor_shift_arrow_key(e, KEY_LEFT);  return EventResult::Handled; }
            if (c == KEY_SHIFT_RIGHT) { editor_shift_arrow_key(e, KEY_RIGHT); return EventResult::Handled; }
            if (c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT) {
                editor_arrow_key(e, c);
                return EventResult::Handled;
            }
            if (c == '\n' || c == '\r') { editor_newline(e); return EventResult::Handled; }
            if (c == '\b' || c == 0x08) { editor_backspace(e); return EventResult::Handled; }
            if (c >= 0x20 && c < 0x7F) { editor_insert_char(e, (char)c); return EventResult::Handled; }
            return EventResult::Handled;
        }

        return EventResult::Ignored;
    }
};

static bool editor_save_as(EditorState* e, EditorWindow* win, const char* name) {
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
        kstrncpy(win->title, title, sizeof(win->title) - 1);
    }
    return ok;
}

// ---- Registro global de janelas de editor abertas ----------------
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
    uint32_t w = FONT_W * (EDITOR_COLS + 5) + (uint32_t)(Window::BORDER*2 + PAD*2);
    uint32_t h = FONT_H * (EDITOR_ROWS + 2) + (uint32_t)(Window::BORDER + Window::TITLE_BAR_H + 1 + PAD*2);

    EditorWindow* win = new EditorWindow(x, y, w, h, tr(STR_EDITOR_TITLE));
    EditorState* e = &win->m_state;

    if (path && path[0]) {
        kstrcpy(e->filename, path);
        VfsNode* node = vfs_resolve(vfs_cwd(), path);
        if (node && node->type == VFS_FILE) {
            e->file = node;
            editor_load_from_node(e, node);
        }
        char title[96];
        kstrcpy(title, tr(STR_EDITOR_TITLE));
        kstrcat(title, " -- ");
        kstrcat(title, path);
        kstrncpy(win->title, title, sizeof(win->title) - 1);
    }

    wm_register(win);
    editor_register(win);
    return win;
}

void editor_tick(Window* win, uint64_t ticks) {
    if (!win) return;
    EditorWindow* ew = static_cast<EditorWindow*>(win);
    EditorState* e = &ew->m_state;
    if (ticks - e->last_blink > 50) {
        e->cursor_visible = !e->cursor_visible;
        e->last_blink = ticks;
    }
    if (e->status_msg[0] && ticks > e->status_until) {
        e->status_msg[0] = 0;
    }
}
