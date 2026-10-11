#include "file_manager.h"
#include "file_manager_fs.h"
#include "editor.h"
#include "../core/wm.h"
#include "../elements/menu.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../kernel/lang.h"
#include "../../kernel/keyboard.h"
#include "terminal.h"
#include "applaunch.h"
#include "../../fs/perm.h"
#include "../../kernel/user.h"

extern "C" volatile uint64_t timer_ticks;

namespace {

constexpr int32_t PAD = 10;
constexpr int32_t TOOLBAR_H = 34;
constexpr int32_t TOOLBAR_BUTTON_W = 58;
constexpr int32_t TOOLBAR_GAP = 6;
constexpr int32_t HEADER_H = 22;
constexpr int32_t ROW_H = 24;
constexpr int32_t STATUS_H = 22;
constexpr uint64_t DOUBLE_CLICK_TICKS = 35;
constexpr uint32_t SCROLL_STEP = 3;     // linhas por "clique" da roda

// Buffer estático para o caminho do .hexec aguardando execução
// diferida (ver wm_defer em open_selected). Precisa ser estático e
// não uma variável local: a ação diferida só roda DEPOIS que
// open_selected() já retornou, então qualquer buffer na pilha dela
// já teria deixado de existir. Um único buffer basta porque o
// usuário só consegue dar um duplo-clique por vez, e a fila é
// drenada a cada frame — bem antes de um segundo duplo-clique ser
// fisicamente possível.
static char s_pending_exec_path[512];

// Área de transferência de ARQUIVOS: guarda o CAMINHO (e não o ponteiro do
// nó), que é re-resolvido na hora de colar — assim um item apagado nesse
// meio tempo vira "não encontrado" em vez de um ponteiro solto.
static char s_clip_path[512];
static bool s_clip_valid = false;
static bool s_clip_cut = false;

// IDs do menu de contexto
enum {
    M_OPEN = 1, M_COPY, M_CUT, M_PASTE, M_RENAME, M_DELETE, M_PROPS,
    M_NEW_DIR, M_NEW_FILE, M_WALLPAPER
};

enum class Mode : uint8_t { Normal, Rename, ConfirmDelete };

// Associação extensão -> programa (abre com duplo-clique/Enter/Abrir). Os
// programas são .hexec em /bin; se faltar, o Explorador avisa. Para trocar o
// programa de uma extensão, mude só esta tabela.
struct Assoc { const char* ext; const char* program; };
static const Assoc k_assoc[] = {
    { ".png", "/bin/imgview.hexec" },
    { ".bmp", "/bin/imgview.hexec" },
};

static int lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static const char* assoc_for(const char* name) {
    size_t l = kstrlen(name);
    for (const Assoc& a : k_assoc) {
        size_t el = kstrlen(a.ext);
        if (l <= el) continue;
        bool same = true;
        for (size_t i = 0; i < el; i++)
            if (lower_c(name[l - el + i]) != a.ext[i]) { same = false; break; }
        if (same) return a.program;
    }
    return nullptr;
}

// Ícone 16x16 por tipo de arquivo, desenhado com retângulos (sem imagens embutidas).
static void draw_file_icon(int32_t x, int32_t y, const VfsNode* n) {
    uint32_t ux = (uint32_t)x, uy = (uint32_t)y;
    if (n->type == VFS_DIR) {                                   // pasta
        fb_fill_rect(ux, uy + 3, 7, 3, 0xD9A93A);
        fb_fill_rect(ux, uy + 5, 16, 10, 0xF2C84B);
        fb_fill_rect(ux, uy + 13, 16, 2, 0xD9A93A);
    } else if (file_manager_is_executable(n)) {                 // programa
        fb_fill_rect(ux + 1, uy + 1, 14, 14, 0x2E9E4E);
        fb_fill_rect(ux + 5, uy + 4, 2, 2, 0xFFFFFF);           // ">"
        fb_fill_rect(ux + 7, uy + 6, 2, 2, 0xFFFFFF);
        fb_fill_rect(ux + 9, uy + 8, 2, 2, 0xFFFFFF);
        fb_fill_rect(ux + 7, uy + 10, 2, 2, 0xFFFFFF);
        fb_fill_rect(ux + 5, uy + 12, 2, 2, 0xFFFFFF);
    } else if (assoc_for(n->name)) {                            // imagem
        fb_fill_rect(ux + 1, uy + 2, 14, 12, 0x3A78C2);
        fb_fill_rect(ux + 2, uy + 3, 12, 10, 0x7FB6F0);
        fb_fill_rect(ux + 10, uy + 4, 3, 3, 0xFFE070);          // sol
        fb_fill_rect(ux + 3, uy + 10, 10, 3, 0x3E9B52);         // montanha
        fb_fill_rect(ux + 5, uy + 8, 6, 2, 0x3E9B52);
        fb_fill_rect(ux + 7, uy + 7, 2, 1, 0x3E9B52);
    } else {                                                    // texto / outro
        fb_fill_rect(ux + 3, uy + 1, 10, 14, 0xE8E8F0);
        fb_fill_rect(ux + 5, uy + 5, 6, 1, 0x7A7F90);
        fb_fill_rect(ux + 5, uy + 8, 6, 1, 0x7A7F90);
        fb_fill_rect(ux + 5, uy + 11, 4, 1, 0x7A7F90);
    }
}

static bool window_alive(Window* w) {
    for (int i = 0; i < wm_window_slot_count(); i++)
        if (wm_get_window_at(i) == w) return w->active;
    return false;
}

// Gera um nome livre em `dir`: "base", depois "stem 2.ext", "stem 3.ext"...
static void make_unique(VfsNode* dir, const char* base, char* out) {
    kmemset(out, 0, VFS_NAME_MAX);
    kstrncpy(out, base, VFS_NAME_MAX - 1);
    if (!vfs_find_child(dir, out)) return;

    char stem[VFS_NAME_MAX], ext[VFS_NAME_MAX];
    kmemset(stem, 0, sizeof(stem));
    kmemset(ext, 0, sizeof(ext));
    size_t len = kstrlen(base);
    int dot = -1;
    for (size_t i = len; i > 1; i--)
        if (base[i - 1] == '.') { dot = (int)(i - 1); break; }
    if (dot > 0) {
        kmemcpy(stem, base, (size_t)dot);
        kstrncpy(ext, base + dot, VFS_NAME_MAX - 1);
    } else {
        kstrncpy(stem, base, VFS_NAME_MAX - 1);
    }
    size_t reserve = 1 + 3 + kstrlen(ext);                 // " " + até 3 dígitos + extensão
    if (reserve >= VFS_NAME_MAX - 1) return;               // extensão absurda: desiste
    size_t room = VFS_NAME_MAX - 1 - reserve;
    if (kstrlen(stem) > room) stem[room] = '\0';

    for (int n = 2; n < 1000; n++) {
        char num[24];
        kitoa(n, num);
        kmemset(out, 0, VFS_NAME_MAX);
        kstrcpy(out, stem);
        kstrcat(out, " ");
        kstrcat(out, num);
        kstrcat(out, ext);
        if (!vfs_find_child(dir, out)) return;
    }
}

// ------------------------------------------------------------
// Janela "Propriedades"
// ------------------------------------------------------------
class PropsWindow : public Window {
public:
    char labels[6][24];
    char values[6][200];
    int  count;

    PropsWindow(int32_t x, int32_t y)
        : Window(x, y, 400, 190, tr(STR_FM_PROPERTIES), WinType::Generic), count(0) {
        min_width = 300;
        min_height = 150;
    }

    void add(const char* label, const char* value) {
        if (count >= 6) return;
        kmemset(labels[count], 0, sizeof(labels[count]));
        kmemset(values[count], 0, sizeof(values[count]));
        kstrncpy(labels[count], label, sizeof(labels[count]) - 1);
        kstrncpy(values[count], value, sizeof(values[count]) - 1);
        count++;
    }

    void draw(int32_t ox, int32_t oy) override {
        Window::draw(ox, oy);
        if (!active || minimized) return;
        Rect c = content_area_absolute();
        for (int i = 0; i < count; i++) {
            uint32_t y = (uint32_t)(c.y + PAD + i * 22);
            fb_draw_string((uint32_t)(c.x + PAD), y, labels[i], COLOR_TEXT_GRAY, 0, true);
            fb_draw_string((uint32_t)(c.x + PAD + 96), y, values[i], COLOR_TEXT_LIGHT, 0, true);
        }
    }

    EventResult on_event(const WidgetEvent& ev) override {
        if (ev.type == EventType::KeyDown) {
            if (ev.key == 27 || ev.key == '\n' || ev.key == '\r') wm_close(this);
            return EventResult::Handled;
        }
        return Window::on_event(ev);
    }
};

static void show_properties(const VfsNode* node, int32_t x, int32_t y) {
    if (!node) return;
    PropsWindow* w = new PropsWindow(x, y);
    if (!w) return;

    char buf[200];
    w->add(tr(STR_FM_P_NAME), node == vfs_root() ? "/" : node->name);
    w->add(tr(STR_FM_P_TYPE), node->type == VFS_DIR ? tr(STR_TERM_STAT_TYPE_DIR)
                                                    : tr(STR_TERM_STAT_TYPE_FILE));
    if (node->type == VFS_FILE) {
        char n[24];
        kitoa((int64_t)node->size, n);
        kstrcpy(buf, n);
        kstrcat(buf, " ");
        kstrcat(buf, tr(STR_FM_P_BYTES));
        w->add(tr(STR_FM_P_SIZE), buf);
    } else {
        char n[24];
        kitoa((int64_t)node->child_count, n);
        w->add(tr(STR_FM_P_ITEMS), n);
    }
    file_manager_path(node, buf, sizeof(buf));
    w->add(tr(STR_FM_P_PATH), buf);

    const UserEntry* u = user_find_uid(node->uid);
    char g[24];
    kitoa((int64_t)node->gid, g);
    kstrcpy(buf, u ? u->name : "?");
    kstrcat(buf, " (");
    kstrcat(buf, g);
    kstrcat(buf, ")");
    w->add(tr(STR_FM_P_OWNER), buf);

    char mode[11];
    perm_format(node, mode);
    w->add(tr(STR_FM_P_MODE), mode);

    if (!wm_register(w)) delete w;
}

// ------------------------------------------------------------
// O gerenciador
// ------------------------------------------------------------
class FileManagerWindow : public Window {
public:
    VfsNode* current_dir;
    VfsNode* selected;
    VfsNode* last_clicked;
    uint64_t last_click_tick;
    char status[96];
    uint32_t scroll;                 // primeira linha visível
    Mode mode;
    VfsNode* edit_node;              // item sendo renomeado
    char edit_buf[VFS_NAME_MAX];
    uint32_t edit_len;

    struct Geo { int32_t left, top, width, list_top; uint32_t rows; };

    // Executada pelo WM num ponto seguro (ver wm_run_deferred_actions),
    // fora de qualquer despacho de evento. `arg` aponta para
    // s_pending_exec_path.
    static void deferred_run_hexec(void* arg) {
        const char* path = (const char*)arg;
        uint32_t sw = fb_width(), sh = fb_height();
        terminal_create_and_run((int32_t)(sw / 2 - 300), (int32_t)(sh / 2 - 200), path);
    }

    FileManagerWindow(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title)
        : Window(x, y, w, h, title, WinType::Generic),
          current_dir(vfs_root()), selected(nullptr), last_clicked(nullptr),
          last_click_tick(0), scroll(0), mode(Mode::Normal), edit_node(nullptr),
          edit_len(0) {
        status[0] = 0;
        edit_buf[0] = 0;
        min_width = 360;
        min_height = 220;
    }

    void set_status(const char* text) {
        kmemset(status, 0, sizeof(status));
        kstrncpy(status, text, sizeof(status) - 1);
    }

    Geo geo() {
        Rect c = content_area_absolute();
        Geo g;
        g.left = c.x + PAD;
        g.top = c.y + PAD;
        g.width = (int32_t)c.w - PAD * 2;
        g.list_top = g.top + TOOLBAR_H + 6 + HEADER_H;
        int32_t list_bottom = c.y + (int32_t)c.h - STATUS_H - PAD;
        int32_t vh = list_bottom - g.list_top;
        if (vh < 0) vh = 0;
        g.rows = (uint32_t)(vh / ROW_H);
        return g;
    }

    void clamp_scroll() {
        uint32_t rows = geo().rows;
        uint32_t max_scroll = current_dir->child_count > rows ? current_dir->child_count - rows : 0;
        if (scroll > max_scroll) scroll = max_scroll;
    }

    int index_of(const VfsNode* node) {
        for (uint32_t i = 0; i < current_dir->child_count; i++)
            if (file_manager_child(current_dir, i) == node) return (int)i;
        return -1;
    }

    void ensure_visible(int idx) {
        if (idx < 0) return;
        uint32_t rows = geo().rows;
        if (rows == 0) return;
        if ((uint32_t)idx < scroll) scroll = (uint32_t)idx;
        else if ((uint32_t)idx >= scroll + rows) scroll = (uint32_t)idx + 1 - rows;
    }

    void go_to(VfsNode* dir) {
        current_dir = dir;
        selected = nullptr;
        last_clicked = nullptr;
        scroll = 0;
        mode = Mode::Normal;
        status[0] = 0;
    }

    // ---------------------------------------------------------
    // Desenho
    // ---------------------------------------------------------
    void draw(int32_t ox, int32_t oy) override {
        kstrncpy(title, tr(STR_FILE_MANAGER_TITLE), sizeof(title) - 1);
        title[sizeof(title) - 1] = 0;
        Window::draw(ox, oy);
        if (!active || minimized) return;

        Rect content = content_area_absolute();
        Geo g = geo();
        int32_t left = g.left;
        int32_t top = g.top;
        int32_t width = g.width;
        if (width < 20) return;
        clamp_scroll();

        fb_fill_rect((uint32_t)left, (uint32_t)top, (uint32_t)width,
                     TOOLBAR_H, 0x142742);
        bool can_go_back = current_dir != vfs_root();
        uint32_t button_color = can_go_back ? 0x244A72 : 0x1B2D46;
        fb_fill_rect((uint32_t)left, (uint32_t)(top + 4),
                 TOOLBAR_BUTTON_W, TOOLBAR_H - 8, button_color);
        fb_draw_string_centered((uint32_t)left, (uint32_t)(top + 4),
                    TOOLBAR_BUTTON_W, TOOLBAR_H - 8,
                    tr(STR_FILE_MANAGER_BACK),
                    can_go_back ? COLOR_TEXT_LIGHT : COLOR_TEXT_GRAY,
                    0, true);

        int32_t home_x = left + TOOLBAR_BUTTON_W + TOOLBAR_GAP;
        fb_fill_rect((uint32_t)home_x, (uint32_t)(top + 4),
                 TOOLBAR_BUTTON_W, TOOLBAR_H - 8, 0x244A72);
        fb_draw_string_centered((uint32_t)home_x, (uint32_t)(top + 4),
                    TOOLBAR_BUTTON_W, TOOLBAR_H - 8,
                    tr(STR_FILE_MANAGER_HOME), COLOR_TEXT_LIGHT,
                    0, true);

        char path[512];
        file_manager_path(current_dir, path, sizeof(path));
        fb_draw_string((uint32_t)(home_x + TOOLBAR_BUTTON_W + TOOLBAR_GAP),
                   (uint32_t)(top + 9),
                       path, COLOR_TEXT_LIGHT, 0, true);

        int32_t header_top = top + TOOLBAR_H + 6;
        int32_t list_top = g.list_top;

        fb_fill_rect((uint32_t)left, (uint32_t)header_top, (uint32_t)width,
                 HEADER_H, 0x1B3554);
        fb_draw_string((uint32_t)(left + 30), (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_NAME), COLOR_TEXT_LIGHT, 0, true);
        fb_draw_string((uint32_t)(left + width - 160),
                   (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_TYPE), COLOR_TEXT_LIGHT, 0, true);
        fb_draw_string((uint32_t)(left + width - 68),
                   (uint32_t)(header_top + 3),
                   tr(STR_FILE_MANAGER_SIZE), COLOR_TEXT_LIGHT, 0, true);

        for (uint32_t r = 0; r < g.rows; r++) {
            uint32_t i = scroll + r;
            if (i >= current_dir->child_count) break;
            VfsNode* child = file_manager_child(current_dir, i);
            if (!child) continue;
            int32_t row_y = list_top + (int32_t)r * ROW_H;
            if (child == selected)
                fb_fill_rect((uint32_t)left, (uint32_t)row_y, (uint32_t)width,
                             ROW_H - 2, 0x244A72);

            const char* marker = child->type == VFS_DIR
                ? tr(STR_TERM_STAT_TYPE_DIR) : tr(STR_TERM_STAT_TYPE_FILE);
            uint32_t marker_color = child->type == VFS_DIR ? COLOR_ACCENT : COLOR_TEXT_GRAY;
            uint32_t name_color = COLOR_TEXT_LIGHT;
            if (child->type == VFS_FILE && file_manager_is_executable(child)) {
                // Destaca executáveis (.hexec) com uma cor diferente na
                // listagem — sinal visual de que um duplo-clique roda o
                // programa em vez de abrir no editor de texto.
                name_color = COLOR_GREEN;
            }
            draw_file_icon(left + 8, row_y + 4, child);
            fb_draw_string((uint32_t)(left + width - 160),
                           (uint32_t)(row_y + 5), marker, marker_color, 0, true);

            if (mode == Mode::Rename && child == edit_node) {
                // Caixa de edição no lugar do nome
                fb_fill_rect((uint32_t)(left + 26), (uint32_t)(row_y + 1),
                             (uint32_t)(width - 206), ROW_H - 4, 0x0C1A2E);
                fb_fill_rect((uint32_t)(left + 26), (uint32_t)(row_y + ROW_H - 4),
                             (uint32_t)(width - 206), 1, COLOR_ACCENT);
                fb_draw_string((uint32_t)(left + 30), (uint32_t)(row_y + 5),
                               edit_buf, COLOR_TEXT_LIGHT, 0, true);
                if ((timer_ticks / 50) % 2 == 0) {
                    uint32_t cx = (uint32_t)(left + 30) + fb_text_width(edit_buf);
                    fb_fill_rect(cx, (uint32_t)(row_y + 4), 2, FONT_H + 2, COLOR_TEXT_LIGHT);
                }
            } else {
                fb_draw_string((uint32_t)(left + 30), (uint32_t)(row_y + 5),
                               child->name, name_color, 0, true);
            }

            if (child->type == VFS_FILE) {
                char size_text[24];
                kitoa((int64_t)child->size, size_text);
                int32_t size_x = left + width - 68;
                fb_draw_string((uint32_t)size_x, (uint32_t)(row_y + 5),
                               size_text, COLOR_TEXT_GRAY, 0, true);
            }
        }

        // Barra de rolagem (só quando a lista não cabe)
        if (g.rows > 0 && current_dir->child_count > g.rows) {
            int32_t track_h = (int32_t)g.rows * ROW_H;
            int32_t thumb_h = track_h * (int32_t)g.rows / (int32_t)current_dir->child_count;
            if (thumb_h < 12) thumb_h = 12;
            int32_t max_scroll = (int32_t)(current_dir->child_count - g.rows);
            int32_t thumb_y = max_scroll > 0
                ? (track_h - thumb_h) * (int32_t)scroll / max_scroll : 0;
            fb_fill_rect((uint32_t)(left + width - 4), (uint32_t)list_top, 3,
                         (uint32_t)track_h, 0x142742);
            fb_fill_rect((uint32_t)(left + width - 4), (uint32_t)(list_top + thumb_y), 3,
                         (uint32_t)thumb_h, COLOR_ACCENT);
        }

        if (current_dir->child_count == 0) {
            fb_draw_string((uint32_t)(left + 8), (uint32_t)(list_top + 5),
                           tr(STR_FILE_MANAGER_EMPTY), COLOR_TEXT_GRAY, 0, true);
        }
        if (status[0]) {
            uint32_t col = mode == Mode::ConfirmDelete ? COLOR_ACCENT : COLOR_TEXT_GRAY;
            fb_draw_string((uint32_t)left,
                           (uint32_t)(content.y + (int32_t)content.h - FONT_H - PAD),
                           status, col, 0, true);
        }
    }

    // ---------------------------------------------------------
    // Operações
    // ---------------------------------------------------------
    void open_selected(VfsNode* node) {
        if (!node) return;
        if (node->type == VFS_DIR) {
            // Entrar numa pasta exige R (listar) e X (atravessar).
            Cred cred = session_cred();
            if (!perm_allows(node, &cred, PERM_R | PERM_X)) {
                set_status(tr(STR_PERM_DENIED));
                return;
            }
            go_to(node);
            return;
        }
        if (file_manager_is_executable(node)) {
            // Duplo-clique num .hexec abre um terminal novo e roda o
            // programa nele (equivalente a digitar "run <arquivo>" —
            // ver terminal_create_and_run), em vez de disparar o
            // processo "às cegas" sem lugar nenhum para mostrar sua
            // saída. O explorador de arquivos não executa nada por
            // conta própria; ele só delega ao terminal, que é quem
            // de fato possui a lógica de execução (process_create +
            // drenagem de stdout).
            //
            // A criação da janela é DIFERIDA (wm_defer) em vez de
            // imediata: pode estarmos dentro de um handler de evento
            // de mouse, ou seja, dentro da iteração que o WM faz sobre
            // sua própria lista de janelas. Criar uma janela aqui
            // modificaria essa lista e o índice de foco no meio da
            // iteração — foi exatamente isso que travava o sistema ao
            // abrir um segundo .hexec sem fechar o terminal anterior.
            // Com wm_defer, a criação acontece no próximo ponto seguro
            // do loop principal, com nenhum despacho de evento em andamento.
            file_manager_path(node, s_pending_exec_path, sizeof(s_pending_exec_path));
            if (wm_defer(&FileManagerWindow::deferred_run_hexec, s_pending_exec_path)) {
                set_status(tr(STR_FILE_MANAGER_EXEC_STARTED));
            } else {
                // Fila de ações diferidas cheia — melhor avisar do que
                // falhar em silêncio.
                set_status(tr(STR_FILE_MANAGER_EXEC_FAILED));
            }
            return;
        }
        {
            Cred cred = session_cred();
            if (!perm_allows(node, &cred, PERM_R)) {
                set_status(tr(STR_PERM_DENIED));
                return;
            }
        }
        if (const char* prog = assoc_for(node->name)) {
            char ipath[512];
            file_manager_path(node, ipath, sizeof(ipath));
            set_status(tr(app_launch(prog, ipath) ? STR_FM_OPENING_VIEWER : STR_FM_NO_VIEWER));
            return;
        }
        if (!file_manager_is_text(node)) {
            set_status(tr(STR_FILE_MANAGER_BINARY));
            return;
        }

        char path[512];
        file_manager_path(node, path, sizeof(path));
        uint32_t sw = fb_width(), sh = fb_height();
        editor_create((int32_t)(sw / 2 - 300), (int32_t)(sh / 2 - 200), path);
        set_status(tr(STR_FILE_MANAGER_OPENED));
    }

    void report(int perm_result, const char* ok_text) {
        if (perm_result == PERM_OK)           set_status(ok_text);
        else if (perm_result == PERM_EACCES)  set_status(tr(STR_PERM_DENIED));
        else                                  set_status(tr(STR_FM_FAILED));
    }

    void begin_rename(VfsNode* node) {
        if (!node || node == vfs_root()) return;
        edit_node = node;
        kmemset(edit_buf, 0, sizeof(edit_buf));
        kstrncpy(edit_buf, node->name, sizeof(edit_buf) - 1);
        edit_len = (uint32_t)kstrlen(edit_buf);
        mode = Mode::Rename;
        status[0] = 0;
    }

    void commit_rename() {
        VfsNode* node = edit_node;
        mode = Mode::Normal;
        edit_node = nullptr;
        if (!node || node->removed) return;
        Cred cred = session_cred();
        int r = vfs_rename_as(&cred, node, current_dir, edit_buf);
        report(r, tr(STR_FM_DONE_RENAMED));
    }

    void begin_delete(VfsNode* node) {
        if (!node || node == vfs_root()) return;
        selected = node;
        mode = Mode::ConfirmDelete;
        kmemset(status, 0, sizeof(status));
        kstrncpy(status, tr(STR_FM_DELETE_Q1), sizeof(status) - 1);
        size_t used = kstrlen(status);
        size_t suffix = kstrlen(tr(STR_FM_DELETE_Q2));
        size_t room = sizeof(status) - 1 > used + suffix ? sizeof(status) - 1 - used - suffix : 0;
        for (size_t i = 0; i < room && node->name[i]; i++) status[used++] = node->name[i];
        status[used] = '\0';
        kstrcat(status, tr(STR_FM_DELETE_Q2));
    }

    void do_delete() {
        VfsNode* node = selected;
        mode = Mode::Normal;
        if (!node) return;
        Cred cred = session_cred();
        int r = vfs_remove_as(&cred, node);
        if (r == PERM_OK) { selected = nullptr; last_clicked = nullptr; clamp_scroll(); }
        report(r, tr(STR_FM_DONE_DELETED));
    }

    void do_copy(bool cut) {
        if (!selected) return;
        file_manager_path(selected, s_clip_path, sizeof(s_clip_path));
        s_clip_valid = true;
        s_clip_cut = cut;
        set_status(tr(cut ? STR_FM_DONE_CUT : STR_FM_DONE_COPIED));
    }

    void do_paste() {
        if (!s_clip_valid) { set_status(tr(STR_FM_CLIP_EMPTY)); return; }
        Cred cred = session_cred();
        VfsNode* src = nullptr;
        if (vfs_resolve_as(&cred, vfs_root(), s_clip_path, &src) != PERM_OK || !src || src->removed) {
            s_clip_valid = false;
            set_status(tr(STR_FM_NOT_FOUND));
            return;
        }
        char name[VFS_NAME_MAX];
        if (s_clip_cut) {
            if (src->parent == current_dir) {            // já está aqui: nada a fazer
                s_clip_valid = false;
                set_status(tr(STR_FM_DONE_PASTED));
                return;
            }
            make_unique(current_dir, src->name, name);
            int r = vfs_rename_as(&cred, src, current_dir, name);
            if (r == PERM_OK) { s_clip_valid = false; selected = src; ensure_visible(index_of(src)); }
            report(r, tr(STR_FM_DONE_PASTED));
        } else {
            make_unique(current_dir, src->name, name);
            VfsNode* copy = nullptr;
            int r = vfs_copy_as(&cred, src, current_dir, name, &copy);
            if (r == PERM_OK && copy) { selected = copy; ensure_visible(index_of(copy)); }
            report(r, tr(STR_FM_DONE_PASTED));
        }
    }

    void do_new(bool folder) {
        Cred cred = session_cred();
        char name[VFS_NAME_MAX];
        make_unique(current_dir, tr(folder ? STR_FM_NEW_FOLDER_NAME : STR_FM_NEW_FILE_NAME), name);
        int err = 0;
        VfsNode* node = folder ? vfs_mkdir_as(&cred, current_dir, name, &err)
                               : vfs_touch_as(&cred, current_dir, name, &err);
        if (!node) { report(err ? err : PERM_EINVAL, ""); return; }
        selected = node;
        ensure_visible(index_of(node));
        begin_rename(node);                               // já abre a edição do nome
    }

    void run_command(int id) {
        switch (id) {
            case M_OPEN:     open_selected(selected); break;
            case M_COPY:     do_copy(false); break;
            case M_CUT:      do_copy(true); break;
            case M_PASTE:    do_paste(); break;
            case M_RENAME:   begin_rename(selected); break;
            case M_DELETE:   begin_delete(selected); break;
            case M_NEW_DIR:  do_new(true); break;
            case M_NEW_FILE: do_new(false); break;
            case M_WALLPAPER: {
                if (!selected) break;
                const char* prog = assoc_for(selected->name);
                if (!prog) break;
                char ipath[512];
                file_manager_path(selected, ipath, sizeof(ipath));
                set_status(tr(app_launch(prog, "--wallpaper", ipath) ? STR_FM_SETTING_WP : STR_FM_NO_VIEWER));
                break;
            }
            case M_PROPS:
                show_properties(selected ? selected : current_dir,
                                bounds.x + 60, bounds.y + 60);
                break;
            default: break;
        }
    }

    static void on_menu(int id, void* user) {
        Window* w = static_cast<Window*>(user);
        if (!window_alive(w)) return;                    // a janela foi fechada com o menu aberto
        static_cast<FileManagerWindow*>(w)->run_command(id);
    }

    void open_context_menu(int32_t mx, int32_t my, VfsNode* node) {
        menu_close();
        menu_pool_reset();
        Menu* m = menu_new();
        if (!m) return;
        uint8_t paste_flag = s_clip_valid ? 0 : MENU_FLAG_DISABLED;
        if (node) {
            menu_add(m, tr(STR_FM_OPEN), M_OPEN);
            if (node->type == VFS_FILE && assoc_for(node->name))
                menu_add(m, tr(STR_FM_SET_WALLPAPER), M_WALLPAPER);
            menu_add_separator(m);
            menu_add(m, tr(STR_FM_COPY), M_COPY, 0, "Ctrl+C");
            menu_add(m, tr(STR_FM_CUT),  M_CUT,  0, "Ctrl+X");
            menu_add(m, tr(STR_FM_PASTE), M_PASTE, paste_flag, "Ctrl+V");
            menu_add_separator(m);
            menu_add(m, tr(STR_FM_RENAME), M_RENAME, 0, "F2");
            menu_add(m, tr(STR_FM_DELETE), M_DELETE, 0, "Del");
            menu_add_separator(m);
            menu_add(m, tr(STR_FM_PROPERTIES), M_PROPS);
        } else {
            menu_add(m, tr(STR_FM_NEW_FOLDER), M_NEW_DIR);
            menu_add(m, tr(STR_FM_NEW_FILE),   M_NEW_FILE);
            menu_add_separator(m);
            menu_add(m, tr(STR_FM_PASTE), M_PASTE, paste_flag, "Ctrl+V");
            menu_add_separator(m);
            menu_add(m, tr(STR_FM_PROPERTIES), M_PROPS);
        }
        menu_popup(m, mx, my, &FileManagerWindow::on_menu, static_cast<Window*>(this));
    }

    // ---------------------------------------------------------
    // Teclado
    // ---------------------------------------------------------
    EventResult on_key(uint8_t k) {
        if (mode == Mode::Rename) {
            if (k == 27) { mode = Mode::Normal; edit_node = nullptr; }
            else if (k == '\n' || k == '\r') commit_rename();
            else if (k == '\b' || k == 0x08) {
                if (edit_len > 0) edit_buf[--edit_len] = '\0';
            } else if (k >= 32 && k < 127 && k != '/' && edit_len < VFS_NAME_MAX - 1) {
                edit_buf[edit_len++] = (char)k;
                edit_buf[edit_len] = '\0';
            }
            return EventResult::Handled;
        }
        if (mode == Mode::ConfirmDelete) {
            if (k == '\n' || k == '\r') do_delete();
            else { mode = Mode::Normal; status[0] = 0; }
            return EventResult::Handled;
        }

        switch (k) {
            case 27:
                wm_close(this);
                break;
            case '\b':
                go_to(file_manager_parent(current_dir));
                break;
            case KEY_F2:        begin_rename(selected); break;
            case KEY_DELETE:    begin_delete(selected); break;
            case KEY_CTRL_C:    do_copy(false); break;
            case KEY_CTRL_X:    do_copy(true); break;
            case KEY_CTRL_V:    do_paste(); break;
            case '\n':
            case '\r':          open_selected(selected); break;
            case KEY_UP:
            case KEY_DOWN: {
                uint32_t n = current_dir->child_count;
                if (n == 0) break;
                int idx = index_of(selected);
                if (k == KEY_DOWN) idx = idx < 0 ? 0 : (idx + 1 < (int)n ? idx + 1 : idx);
                else               idx = idx < 0 ? (int)n - 1 : (idx > 0 ? idx - 1 : 0);
                selected = file_manager_child(current_dir, (uint32_t)idx);
                ensure_visible(idx);
                break;
            }
            default: break;
        }
        return EventResult::Handled;
    }

    // ---------------------------------------------------------
    // Mouse
    // ---------------------------------------------------------
    // Linha sob (mx,my) ou nullptr; *in_list diz se o ponto está na área da lista.
    VfsNode* node_at(int32_t mx, int32_t my, bool* in_list) {
        Geo g = geo();
        Rect content = content_area_absolute();
        int32_t list_bottom = g.list_top + (int32_t)g.rows * ROW_H;
        bool inside = mx >= g.left && mx < g.left + g.width && my >= g.list_top &&
                      my < content.y + (int32_t)content.h - STATUS_H - PAD;
        if (in_list) *in_list = inside;
        if (!inside || my >= list_bottom) return nullptr;
        uint32_t idx = scroll + (uint32_t)((my - g.list_top) / ROW_H);
        if (idx >= current_dir->child_count) return nullptr;
        return file_manager_child(current_dir, idx);
    }

    EventResult on_event(const WidgetEvent& ev) override {
        if (ev.type == EventType::KeyDown) return on_key(ev.key);

        if (ev.type == EventType::MouseScroll) {
            int32_t delta = ev.scroll * (int32_t)SCROLL_STEP;      // positivo = para cima
            if (delta > 0) scroll = scroll > (uint32_t)delta ? scroll - (uint32_t)delta : 0;
            else           scroll += (uint32_t)(-delta);
            clamp_scroll();
            return EventResult::Handled;
        }

        if (ev.type == EventType::MouseRightDown) {
            Rect content = content_area_absolute();
            if (!content.contains(ev.x, ev.y)) return EventResult::Ignored;
            if (mode != Mode::Normal) { mode = Mode::Normal; edit_node = nullptr; status[0] = 0; }
            bool in_list = false;
            VfsNode* node = node_at(ev.x, ev.y, &in_list);
            if (!in_list) return EventResult::Handled;             // barra de ferramentas/cabeçalho
            selected = node;                                        // nullptr = área vazia
            open_context_menu(ev.x, ev.y, node);
            return EventResult::Handled;
        }

        if (ev.type != EventType::MouseDown) return EventResult::Ignored;

        Rect content = content_area_absolute();
        int32_t mx = ev.x;
        int32_t my = ev.y;
        if (!content.contains(mx, my)) return EventResult::Ignored;

        // Qualquer clique cancela uma edição/confirmação em andamento
        // (clicar no próprio campo de edição não faz nada de especial).
        if (mode != Mode::Normal) {
            if (mode == Mode::Rename) commit_rename();
            else { mode = Mode::Normal; status[0] = 0; }
        }

        Geo g = geo();
        int32_t left = g.left;
        int32_t top = g.top;
        int32_t home_x = left + TOOLBAR_BUTTON_W + TOOLBAR_GAP;
        if (my >= top + 4 && my < top + TOOLBAR_H - 4 &&
            mx >= left && mx < left + TOOLBAR_BUTTON_W) {
            go_to(file_manager_parent(current_dir));
            return EventResult::Handled;
        }
        if (my >= top + 4 && my < top + TOOLBAR_H - 4 &&
            mx >= home_x && mx < home_x + TOOLBAR_BUTTON_W) {
            // "Home" leva à pasta pessoal do usuário logado (antes ia para a
            // raiz, quando todos compartilhavam o mesmo /home/user).
            Cred hcred = session_cred();
            VfsNode* home = nullptr;
            if (vfs_resolve_as(&hcred, vfs_root(), session_home(), &home) != PERM_OK || !home)
                home = vfs_root();
            go_to(home);
            return EventResult::Handled;
        }

        if (my < g.list_top) return EventResult::Handled;
        bool in_list = false;
        VfsNode* node = node_at(mx, my, &in_list);
        if (!node) {
            selected = nullptr;
            return EventResult::Handled;
        }

        uint64_t now = timer_ticks;
        bool double_click = node == last_clicked &&
                    now >= last_click_tick &&
                    now - last_click_tick <= DOUBLE_CLICK_TICKS;
        selected = node;
        last_clicked = node;
        last_click_tick = now;
        if (double_click) open_selected(node);
        return EventResult::Handled;
    }
};

} // namespace

Window* file_manager_create(int32_t x, int32_t y) {
    FileManagerWindow* win = new FileManagerWindow(x, y, 620, 440,
                                                    tr(STR_FILE_MANAGER_TITLE));
    if (!win) return nullptr; // heap esgotada — ver terminal_create
    if (!wm_register(win)) {
        delete win;
        return nullptr;
    }
    return win;
}
