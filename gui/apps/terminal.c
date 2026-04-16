#include "terminal.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../drivers/rtc.h"
#include "../../fs/vfs.h"

#define PAD 8

// ---- Histórico de comandos ----------------------------------
#define TERM_CMD_HISTORY 20

typedef struct {
    char lines[TERM_HIST][TERM_COLS + 1];
    int  num_lines;
    int  scroll_offset;

    char input[TERM_BUF_SIZE];
    int  input_len;

    bool     cursor_visible;
    uint64_t last_blink;

    // Histórico de comandos (seta para cima/baixo)
    char cmd_history[TERM_CMD_HISTORY][TERM_BUF_SIZE];
    int  cmd_hist_count;
    int  cmd_hist_pos;   // -1 = linha atual
} TermState;

// ---- Utilitários internos -----------------------------------

static void term_scroll(TermState* t) {
    for (int i = 0; i < TERM_HIST - 1; i++)
        kmemcpy(t->lines[i], t->lines[i+1], TERM_COLS + 1);
    kmemset(t->lines[TERM_HIST - 1], 0, TERM_COLS + 1);
    if (t->num_lines > 0) t->num_lines--;
}

static void term_newline(TermState* t) {
    if (t->num_lines >= TERM_HIST) term_scroll(t);
    else t->num_lines++;
    int row = t->num_lines - 1;
    if (row >= 0 && row < TERM_HIST) kmemset(t->lines[row], 0, TERM_COLS + 1);
}

static void term_print(TermState* t, const char* s) {
    while (*s) {
        if (*s == '\n') {
            term_newline(t);
        } else {
            int row = (t->num_lines > 0) ? t->num_lines - 1 : 0;
            if (row >= TERM_HIST) { term_scroll(t); row = TERM_HIST - 1; }
            size_t len = kstrlen(t->lines[row]);
            if (len < TERM_COLS) {
                t->lines[row][len]   = *s;
                t->lines[row][len+1] = 0;
            }
        }
        s++;
    }
}

static void term_println(TermState* t, const char* s) {
    term_print(t, s);
    term_newline(t);
}


static void parse_command(const char* input, char* cmd_out, size_t cmd_size, const char** args_out) {

    while (*input == ' ') input++;
    
    size_t i = 0;
    while (*input && *input != ' ' && i < cmd_size - 1) {
        cmd_out[i++] = *input++;
    }
    cmd_out[i] = '\0';
 
    while (*input == ' ') input++;
    *args_out = input; 
}


static void cmd_help(TermState* t) {
    term_println(t, "Comandos do HAOS Shell:");
    term_println(t, "  help          -- esta ajuda");
    term_println(t, "  clear         -- limpa a tela");
    term_println(t, "  echo <texto>  -- imprime texto");
    term_println(t, "  about         -- sobre o HAOS");
    term_println(t, "  date          -- data/hora atual");
    term_println(t, "  mem           -- informacoes de memoria");
    term_println(t, "  reboot        -- reinicia o sistema");
    term_println(t, "");
    term_println(t, "Sistema de Arquivos:");
    term_println(t, "  pwd           -- diretorio atual");
    term_println(t, "  ls [dir]      -- lista arquivos");
    term_println(t, "  cd <dir>      -- muda diretorio");
    term_println(t, "  mkdir <nome>  -- cria diretorio");
    term_println(t, "  touch <nome>  -- cria arquivo vazio");
    term_println(t, "  cat <arq>     -- exibe conteudo");
    term_println(t, "  write <arq> <texto> -- escreve em arquivo");
    term_println(t, "  append <arq> <texto> -- acrescenta ao arquivo");
    term_println(t, "  rm <nome>     -- remove arquivo/diretorio");
    term_println(t, "  stat <nome>   -- informacoes do arquivo");
}

static void cmd_about(TermState* t) {
    term_println(t, "");
    term_println(t, "  HAOS v1.2 -- Home-built OS");
    term_println(t, "  Arquitetura : x86-64 (Long Mode)");
    term_println(t, "  Boot        : GRUB2 + Multiboot2");
    term_println(t, "  Video       : VESA VBE 1024x768 32bpp");
    term_println(t, "  GUI         : double-buffered framebuffer");
    term_println(t, "  Input       : PS/2 Keyboard + Mouse");
    term_println(t, "  FS          : HAOS-VFS (heap tree)");
    term_println(t, "");
}

static void cmd_date(TermState* t) {
    rtc_time_t rt;
    rtc_read_time(&rt);

    char date_buf[12];
    rtc_format_date(date_buf, &rt);
    char time_buf[9];
    rtc_format_time(time_buf, &rt);

    char line[TERM_COLS + 1];
    kmemset(line, 0, sizeof(line));
    kstrcpy(line, "  Data: ");
    kstrcat(line, date_buf);
    kstrcat(line, "  Hora: ");
    kstrcat(line, time_buf);
    term_println(t, line);
}

static void cmd_mem(TermState* t) {
    term_println(t, "  Heap: 12 MB (bump allocator)");
    term_println(t, "  Shadow buffer: ~3 MB (double buffering)");
    term_println(t, "  BG cache: ~3 MB");
    term_println(t, "  VFS: alocado na heap");
}

static void cmd_reboot(TermState* t) {
    (void)t;
    outb(0x64, 0xFE);
    while(1) __asm__("hlt");
}

// ---- Comandos de FS -----------------------------------------

static void cmd_pwd(TermState* t) {
    char buf[512];
    vfs_path_of(vfs_cwd(), buf, sizeof(buf));
    term_println(t, buf);
}

static void cmd_ls(TermState* t, const char* path_arg) {
    VfsNode* dir;
    if (path_arg && path_arg[0]) {
        dir = vfs_resolve(vfs_cwd(), path_arg);
        if (!dir) { term_println(t, "ls: diretorio nao encontrado"); return; }
        if (dir->type != VFS_DIR) { term_println(t, "ls: nao e um diretorio"); return; }
    } else {
        dir = vfs_cwd();
    }
    if (dir->child_count == 0) { term_println(t, "  (diretorio vazio)"); return; }

    for (uint32_t i = 0; i < dir->child_count; i++) {
        VfsNode* child = dir->children[i];
        char line[TERM_COLS + 1];
        kmemset(line, 0, sizeof(line));
        if (child->type == VFS_DIR) kstrcpy(line, "  [DIR]  ");
        else                        kstrcpy(line, "  [ARQ]  ");
        kstrcat(line, child->name);
        if (child->type == VFS_FILE) {
            kstrcat(line, "  (");
            char sz[16];
            kitoa((int64_t)child->size, sz);
            kstrcat(line, sz);
            kstrcat(line, " B)");
        }
        term_println(t, line);
    }
}

static void cmd_cd(TermState* t, const char* path_arg) {
    if (!path_arg || !path_arg[0]) {
        VfsNode* home = vfs_resolve(vfs_root(), "/home/user");
        if (!home) home = vfs_root();
        vfs_set_cwd(home);
        return;
    }
    VfsNode* target = vfs_resolve(vfs_cwd(), path_arg);
    if (!target) { term_println(t, "cd: diretorio nao encontrado"); return; }
    if (target->type != VFS_DIR) { term_println(t, "cd: nao e um diretorio"); return; }
    vfs_set_cwd(target);
}

static void cmd_mkdir(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "mkdir: nome necessario"); return; }
    VfsNode* n = vfs_mkdir(vfs_cwd(), name);
    if (!n) term_println(t, "mkdir: falha (ja existe ou limite atingido)");
}

static void cmd_touch(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "touch: nome necessario"); return; }
    VfsNode* n = vfs_touch(vfs_cwd(), name);
    if (!n) term_println(t, "touch: falha ao criar arquivo");
}

static void cmd_cat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "cat: nome necessario"); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, "cat: arquivo nao encontrado"); return; }
    if (node->type == VFS_DIR) { term_println(t, "cat: e um diretorio"); return; }
    if (!node->data || node->size == 0) { term_println(t, "(arquivo vazio)"); return; }
    const char* p = node->data;
    char line[TERM_COLS + 1];
    int li = 0;
    while (*p) {
        if (*p == '\n' || li >= TERM_COLS) {
            line[li] = 0;
            term_println(t, line);
            li = 0;
        } else {
            line[li++] = *p;
        }
        p++;
    }
    if (li > 0) { line[li] = 0; term_println(t, line); }
}

static void cmd_write(TermState* t, const char* rest) {
    // rest já contém "<arq> <texto>"
    while (*rest == ' ') rest++;
    if (!*rest) { term_println(t, "write: uso: write <arq> <texto>"); return; }
    
    // Extrai nome do arquivo (primeira palavra)
    char fname[VFS_NAME_MAX];
    int fi = 0;
    while (*rest && *rest != ' ' && fi < (int)(VFS_NAME_MAX) - 1) {
        fname[fi++] = *rest++;
    }
    fname[fi] = 0;
    
    // Pula espaços para chegar ao texto
    while (*rest == ' ') rest++;
    
    if (!fname[0]) { term_println(t, "write: nome vazio"); return; }
    
    VfsNode* node = vfs_resolve(vfs_cwd(), fname);
    if (!node) node = vfs_touch(vfs_cwd(), fname);
    if (!node || node->type != VFS_FILE) { term_println(t, "write: nao e um arquivo"); return; }
    
    vfs_write(node, rest);
    vfs_append(node, "\n");
    term_println(t, "  escrito.");
}

static void cmd_append(TermState* t, const char* rest) {
    while (*rest == ' ') rest++;
    if (!*rest) { term_println(t, "append: uso: append <arq> <texto>"); return; }
    
    char fname[VFS_NAME_MAX];
    int fi = 0;
    while (*rest && *rest != ' ' && fi < (int)(VFS_NAME_MAX) - 1) {
        fname[fi++] = *rest++;
    }
    fname[fi] = 0;
    
    while (*rest == ' ') rest++;
    
    VfsNode* node = vfs_resolve(vfs_cwd(), fname);
    if (!node) node = vfs_touch(vfs_cwd(), fname);
    if (!node || node->type != VFS_FILE) { term_println(t, "append: nao e um arquivo"); return; }
    
    vfs_append(node, rest);
    vfs_append(node, "\n");
    term_println(t, "  acrescentado.");
}

static void cmd_rm(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "rm: nome necessario"); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, "rm: nao encontrado"); return; }
    if (node == vfs_root()) { term_println(t, "rm: nao posso remover o root"); return; }
    if (node == vfs_cwd()) { term_println(t, "rm: nao posso remover o diretorio atual"); return; }
    vfs_rm(node);
    term_println(t, "  removido.");
}

static void cmd_stat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "stat: nome necessario"); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, "stat: nao encontrado"); return; }
    char buf[TERM_COLS + 1];
    kstrcpy(buf, "  Nome : "); kstrcat(buf, node->name);
    term_println(t, buf);
    kstrcpy(buf, "  Tipo : ");
    kstrcat(buf, node->type == VFS_DIR ? "diretorio" : "arquivo");
    term_println(t, buf);
    if (node->type == VFS_FILE) {
        char sz[24]; kitoa((int64_t)node->size, sz);
        kstrcpy(buf, "  Tam  : "); kstrcat(buf, sz); kstrcat(buf, " bytes");
        term_println(t, buf);
    } else {
        char cnt[24]; kitoa((int64_t)node->child_count, cnt);
        kstrcpy(buf, "  Itens: "); kstrcat(buf, cnt);
        term_println(t, buf);
    }
    char path[512];
    vfs_path_of(node, path, sizeof(path));
    kstrcpy(buf, "  Path : "); kstrcat(buf, path);
    term_println(t, buf);
}

// ---- Prompt dinâmico ----------------------------------------

#define PROMPT_MAX 40

static void term_build_prompt(char* buf, size_t bufsz) {
    char path[256];
    vfs_path_of(vfs_cwd(), path, sizeof(path));
    size_t len = 0;
    const char* prefix = "haos:";
    while (*prefix && len < bufsz - 3) buf[len++] = *prefix++;
    const char* p = path;
    while (*p && len < bufsz - 3) buf[len++] = *p++;
    buf[len++] = '>';
    buf[len++] = ' ';
    buf[len] = 0;
}

// ---- Dispatcher ------------------------------------

static void term_execute(TermState* t, const char* input) {
    char cmd[32];
    const char* args;
    
    parse_command(input, cmd, sizeof(cmd), &args);
    
    // ignora input se ele for vazio
    if (cmd[0] == '\0') return;
    
    if (t->cmd_hist_count < TERM_CMD_HISTORY) {
        kstrcpy(t->cmd_history[t->cmd_hist_count++], input);
    } else {
        for (int i = 0; i < TERM_CMD_HISTORY - 1; i++)
            kmemcpy(t->cmd_history[i], t->cmd_history[i+1], TERM_BUF_SIZE);
        kstrcpy(t->cmd_history[TERM_CMD_HISTORY - 1], input);
    }
    t->cmd_hist_pos = -1;
    
    // Dispatch baseado apenas no nome do comando
    if (kstrcmp(cmd, "help") == 0) {
        cmd_help(t);
    } else if (kstrcmp(cmd, "clear") == 0) {
        for (int i = 0; i < TERM_HIST; i++) kmemset(t->lines[i], 0, TERM_COLS+1);
        t->num_lines = 1;
    } else if (kstrcmp(cmd, "echo") == 0) {
        term_println(t, args);  // imprime todos os argumentos
    } else if (kstrcmp(cmd, "about") == 0) {
        cmd_about(t);
    } else if (kstrcmp(cmd, "date") == 0) {
        cmd_date(t);
    } else if (kstrcmp(cmd, "mem") == 0) {
        cmd_mem(t);
    } else if (kstrcmp(cmd, "reboot") == 0) {
        cmd_reboot(t);
    } else if (kstrcmp(cmd, "pwd") == 0) {
        cmd_pwd(t);
    } else if (kstrcmp(cmd, "ls") == 0) {
        cmd_ls(t, args);
    } else if (kstrcmp(cmd, "cd") == 0) {
        cmd_cd(t, args);
    } else if (kstrcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(t, args);
    } else if (kstrcmp(cmd, "touch") == 0) {
        cmd_touch(t, args);
    } else if (kstrcmp(cmd, "cat") == 0) {
        cmd_cat(t, args);
    } else if (kstrcmp(cmd, "write") == 0) {
        cmd_write(t, args);
    } else if (kstrcmp(cmd, "append") == 0) {
        cmd_append(t, args);
    } else if (kstrcmp(cmd, "rm") == 0) {
        cmd_rm(t, args);
    } else if (kstrcmp(cmd, "stat") == 0) {
        cmd_stat(t, args);
    } else {
        char buf[TERM_COLS + 1];
        kstrcpy(buf, "  Comando nao encontrado: ");
        kstrcat(buf, cmd);
        term_println(t, buf);
        term_println(t, "  Digite 'help' para ver os comandos.");
    }
}

// ---- Renderização --------------------------------------------

static void term_draw(Window* win) {
    TermState* t = (TermState*)win->content;
    if (!t) return;

    int bx = win->x + WIN_BORDER + PAD;
    int by = win->y + WIN_BORDER + TITLE_BAR_H + 1 + PAD;
    int bw = (int)win->w - WIN_BORDER*2 - PAD*2;
    int bh = (int)win->h - WIN_BORDER - TITLE_BAR_H - 1 - PAD*2;

    int visible_rows = bh / FONT_H;
    if (visible_rows < 1) visible_rows = 1;

    int start_line = t->num_lines - visible_rows + 1;
    if (start_line < 0) start_line = 0;

    for (int r = 0; r < visible_rows - 1; r++) {
        int line_idx = start_line + r;
        int ry = by + r * FONT_H;
        if (line_idx < 0 || line_idx >= TERM_HIST) continue;
        char* text = t->lines[line_idx];

        fb_fill_rect((uint32_t)bx, (uint32_t)ry, (uint32_t)bw, FONT_H, COLOR_TERM_BG);
        if (!text[0]) continue;

        if (kstrncmp(text, "haos:", 5) == 0) {
            // Acha fim do prompt "haos:...> "
            const char* p = text + 5;
            int plen = 5;
            while (*p && !(*p == '>' && *(p+1) == ' ')) { p++; plen++; }
            if (*p) plen += 2;

            char prompt_part[PROMPT_MAX + 1];
            int copy = plen < PROMPT_MAX ? plen : PROMPT_MAX;
            kmemcpy(prompt_part, text, (size_t)copy);
            prompt_part[copy] = 0;

            fb_draw_string((uint32_t)bx, (uint32_t)ry,
                           prompt_part, COLOR_TERM_PROMPT, 0, true);
            fb_draw_string((uint32_t)(bx + FONT_W * copy), (uint32_t)ry,
                           text + copy, COLOR_TERM_CMD, 0, true);
        } else {
            fb_draw_string((uint32_t)bx, (uint32_t)ry,
                           text, COLOR_TERM_FG, 0, true);
        }
    }

    // Linha de input
    int input_y = by + (visible_rows - 1) * FONT_H;
    fb_fill_rect((uint32_t)bx, (uint32_t)input_y, (uint32_t)bw, FONT_H, COLOR_TERM_BG);

    char prompt[PROMPT_MAX + 1];
    term_build_prompt(prompt, sizeof(prompt));
    int prompt_len = (int)kstrlen(prompt);

    fb_draw_string((uint32_t)bx, (uint32_t)input_y,
                   prompt, COLOR_TERM_PROMPT, 0, true);
    if (t->input_len > 0)
        fb_draw_string((uint32_t)(bx + FONT_W * prompt_len), (uint32_t)input_y,
                       t->input, COLOR_TEXT_LIGHT, 0, true);

    int cx = bx + FONT_W * (prompt_len + t->input_len);
    uint32_t cur_color = t->cursor_visible ? COLOR_TERM_PROMPT : COLOR_TERM_BG;
    fb_fill_rect((uint32_t)cx, (uint32_t)(input_y + 1), 2, FONT_H - 3, cur_color);
}

// ---- Teclas ------------------------------------

#define KEY_UP   0x80
#define KEY_DOWN 0x81

static void term_key(Window* win, uint8_t c) {
    TermState* t = (TermState*)win->content;
    if (!t) return;

    if (c == '\n' || c == '\r') {
        char prompt[PROMPT_MAX + 1];
        term_build_prompt(prompt, sizeof(prompt));
        char echo_line[TERM_COLS + PROMPT_MAX + 2];
        kstrcpy(echo_line, prompt);
        kstrcat(echo_line, t->input);
        term_println(t, echo_line);
        term_execute(t, t->input);
        kmemset(t->input, 0, TERM_BUF_SIZE);
        t->input_len = 0;
        t->cmd_hist_pos = -1;

    } else if (c == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            t->input[t->input_len] = 0;
        }

    } else if (c == KEY_UP) {
        if (t->cmd_hist_count == 0) return;
        if (t->cmd_hist_pos < 0) t->cmd_hist_pos = t->cmd_hist_count - 1;
        else if (t->cmd_hist_pos > 0) t->cmd_hist_pos--;
        kstrcpy(t->input, t->cmd_history[t->cmd_hist_pos]);
        t->input_len = (int)kstrlen(t->input);

    } else if (c == KEY_DOWN) {
        if (t->cmd_hist_pos < 0) return;
        t->cmd_hist_pos++;
        if (t->cmd_hist_pos >= t->cmd_hist_count) {
            t->cmd_hist_pos = -1;
            kmemset(t->input, 0, TERM_BUF_SIZE);
            t->input_len = 0;
        } else {
            kstrcpy(t->input, t->cmd_history[t->cmd_hist_pos]);
            t->input_len = (int)kstrlen(t->input);
        }

    } else if (c >= 0x20 && c < 0x7F) {
        if (t->input_len < TERM_BUF_SIZE - 1) {
            t->input[t->input_len++] = c;
            t->input[t->input_len]   = 0;
        }
    }
}

// ---- Criação -----------------------------------

Window* terminal_create(int32_t x, int32_t y) {
    uint32_t w = FONT_W * TERM_COLS + (uint32_t)(WIN_BORDER*2 + PAD*2);
    uint32_t h = FONT_H * (TERM_ROWS + 1) + (uint32_t)(WIN_BORDER + TITLE_BAR_H + 1 + PAD*2);

    Window* win = wm_create(WIN_TYPE_TERMINAL, x, y, w, h, "Terminal -- HAOS Shell v1.2");
    if (!win) return NULL;

    TermState* t = (TermState*)kzalloc(sizeof(TermState));
    if (!t) return win;

    t->num_lines      = 1;
    t->cursor_visible = true;
    t->last_blink     = 0;
    t->cmd_hist_pos   = -1;

    term_println(t, "  HAOS Shell v1.2  --  Digite 'help' para ajuda");
    term_println(t, "  Use o mouse para arrastar esta janela.");
    term_println(t, "");

    win->content      = t;
    win->draw_content = term_draw;
    win->on_key       = term_key;
    return win;
}

void terminal_tick(Window* win, uint64_t ticks) {
    if (!win || !win->content) return;
    TermState* t = (TermState*)win->content;
    if (ticks - t->last_blink > 50) {
        t->cursor_visible = !t->cursor_visible;
        t->last_blink = ticks;
    }
}