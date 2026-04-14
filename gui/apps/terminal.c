// gui/apps/terminal.c — terminal com shell embutido (input corrigido)
#include "terminal.h"
#include "../window.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"

#define PAD 8

typedef struct {
    char lines[TERM_HIST][TERM_COLS + 1];
    int  num_lines;
    int  scroll_offset;

    char input[TERM_BUF_SIZE];
    int  input_len;

    bool     cursor_visible;
    uint64_t last_blink;
} TermState;

// ---- Utilitários internos ------------------------------------

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

// ---- Comandos ------------------------------------------------

static void cmd_help(TermState* t) {
    term_println(t, "Comandos disponíveis:");
    term_println(t, "  help      — exibe esta ajuda");
    term_println(t, "  clear     — limpa a tela");
    term_println(t, "  echo ...  — imprime texto");
    term_println(t, "  about     — sobre o HAOS");
    term_println(t, "  ls        — lista arquivos (simulado)");
    term_println(t, "  date      — exibe data/hora");
    term_println(t, "  mem       — info de memória");
    term_println(t, "  color     — teste de cores");
    term_println(t, "  reboot    — reinicia o sistema");
}

static void cmd_about(TermState* t) {
    term_println(t, "");
    term_println(t, "  ██╗  ██╗ █████╗  ██████╗ ███████╗");
    term_println(t, "  ██║  ██║██╔══██╗██╔═══██╗██╔════╝");
    term_println(t, "  ███████║███████║██║   ██║███████╗");
    term_println(t, "  ██╔══██║██╔══██║██║   ██║╚════██║");
    term_println(t, "  ██║  ██║██║  ██║╚██████╔╝███████║");
    term_println(t, "  ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝");
    term_println(t, "");
    term_println(t, "  HAOS v1.1 — Home-built OS");
    term_println(t, "  Arquitetura : x86-64 (Long Mode)");
    term_println(t, "  Boot        : GRUB2 + Multiboot2");
    term_println(t, "  Video       : VESA VBE 1024x768 32bpp");
    term_println(t, "  GUI         : double-buffered framebuffer");
    term_println(t, "  Input       : PS/2 Keyboard + Mouse");
    term_println(t, "");
}

static void cmd_ls(TermState* t) {
    term_println(t, "  boot/        kernel/       drivers/");
    term_println(t, "  gui/         README.txt    haos.iso");
}

static void cmd_date(TermState* t) {
    term_println(t, "  Data: --/--/-- (RTC nao implementado em v1.1)");
}

static void cmd_mem(TermState* t) {
    term_println(t, "  Heap: 12 MB (bump allocator)");
    term_println(t, "  Shadow buffer: ~3 MB (double buffering)");
    term_println(t, "  BG cache: ~3 MB");
}

static void cmd_color(TermState* t) {
    term_println(t, "  Paleta HAOS v1.1:");
    term_println(t, "  Accent: #5AA0FF  BG: #0B0E1A  Term: #0D1117");
    term_println(t, "  FG: #00FF99  Prompt: #5AA0FF  Cmd: #FFDD44");
}

static void cmd_reboot(TermState* t) {
    (void)t;
    outb(0x64, 0xFE);
    while(1) __asm__("hlt");
}

static void term_execute(TermState* t, const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    if      (kstrcmp(cmd, "help")  == 0) cmd_help(t);
    else if (kstrcmp(cmd, "clear") == 0) {
        for (int i = 0; i < TERM_HIST; i++) kmemset(t->lines[i], 0, TERM_COLS+1);
        t->num_lines = 1;
    }
    else if (kstrncmp(cmd, "echo ", 5) == 0) term_println(t, cmd + 5);
    else if (kstrcmp(cmd, "about")  == 0) cmd_about(t);
    else if (kstrcmp(cmd, "ls")     == 0) cmd_ls(t);
    else if (kstrcmp(cmd, "date")   == 0) cmd_date(t);
    else if (kstrcmp(cmd, "mem")    == 0) cmd_mem(t);
    else if (kstrcmp(cmd, "color")  == 0) cmd_color(t);
    else if (kstrcmp(cmd, "reboot") == 0) cmd_reboot(t);
    else {
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

        if (kstrncmp(text, "haos> ", 6) == 0) {
            fb_draw_string((uint32_t)bx, (uint32_t)ry,
                           "haos> ", COLOR_TERM_PROMPT, 0, true);
            fb_draw_string((uint32_t)(bx + FONT_W*6), (uint32_t)ry,
                           text + 6, COLOR_TERM_CMD, 0, true);
        } else {
            fb_draw_string((uint32_t)bx, (uint32_t)ry,
                           text, COLOR_TERM_FG, 0, true);
        }
    }

    // Linha de input
    int input_y = by + (visible_rows - 1) * FONT_H;
    fb_fill_rect((uint32_t)bx, (uint32_t)input_y, (uint32_t)bw, FONT_H, COLOR_TERM_BG);

    fb_draw_string((uint32_t)bx, (uint32_t)input_y,
                   "haos> ", COLOR_TERM_PROMPT, 0, true);
    if (t->input_len > 0)
        fb_draw_string((uint32_t)(bx + FONT_W*6), (uint32_t)input_y,
                       t->input, COLOR_TEXT_LIGHT, 0, true);

    int cx = bx + FONT_W * (6 + t->input_len);
    uint32_t cur_color = t->cursor_visible ? COLOR_TERM_PROMPT : COLOR_TERM_BG;
    fb_fill_rect((uint32_t)cx, (uint32_t)(input_y + 1), 2, FONT_H - 3, cur_color);
}

// ---- Tratamento de teclas ------------------------------------

static void term_key(Window* win, uint8_t c) {
    TermState* t = (TermState*)win->content;
    if (!t) return;

    if (c == '\n' || c == '\r') {
        char echo_line[TERM_COLS + 8];
        kstrcpy(echo_line, "haos> ");
        kstrcat(echo_line, t->input);
        term_println(t, echo_line);
        term_execute(t, t->input);
        kmemset(t->input, 0, TERM_BUF_SIZE);
        t->input_len = 0;
    } else if (c == '\b') {
        if (t->input_len > 0) {
            t->input_len--;
            t->input[t->input_len] = 0;
        }
    } else if (c >= 0x20 && c < 0x7F) {
        if (t->input_len < TERM_BUF_SIZE - 1) {
            t->input[t->input_len++] = c;
            t->input[t->input_len]   = 0;
        }
    }
}

// ---- Criação -------------------------------------------------

Window* terminal_create(int32_t x, int32_t y) {
    uint32_t w = FONT_W * TERM_COLS + (uint32_t)(WIN_BORDER*2 + PAD*2);
    uint32_t h = FONT_H * (TERM_ROWS + 1) + (uint32_t)(WIN_BORDER + TITLE_BAR_H + 1 + PAD*2);

    Window* win = wm_create(WIN_TYPE_TERMINAL, x, y, w, h, "Terminal — HAOS Shell v1.1");
    if (!win) return NULL;

    TermState* t = (TermState*)kzalloc(sizeof(TermState));
    if (!t) return win;

    t->num_lines      = 1;
    t->cursor_visible = true;
    t->last_blink     = 0;

    term_println(t, "  HAOS Shell v1.1  —  Digite 'help' para ajuda");
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