#include "terminal.h"
#include "../core/wm.h"
#include "../../drivers/fb.h"
#include "../../drivers/font.h"
#include "../../kernel/memory.h"
#include "../../kernel/types.h"
#include "../../kernel/keyboard.h"
#include "../../drivers/rtc.h"
#include "../../fs/vfs.h"
#include "../../kernel/lang.h"
#include "editor.h"
#include "../widgets_basic.h"
#include "../../kernel/process.h"

#define PAD 8
#define TERM_CMD_HISTORY 20
#define PROMPT_MAX 40

// ---- Estado do terminal (era TermState + void* win->content;
//      agora é membro direto de TerminalWindow) ----
struct TermState {
    char lines[TERM_HIST][TERM_COLS + 1];
    int  num_lines;
    int  scroll_offset;

    char input[TERM_BUF_SIZE];
    int  input_len;

    bool     cursor_visible;
    uint64_t last_blink;

    char cmd_history[TERM_CMD_HISTORY][TERM_BUF_SIZE];
    int  cmd_hist_count;
    int  cmd_hist_pos;

    // ---- Processo de usuário rodando "dentro" deste terminal ----
    // Só um processo por terminal — cada terminal mostra a saída de
    // um programa de cada vez. Desde a Fase 3 (isolamento de espaço
    // de endereço, ver hexec.h/paging.h), isso NÃO limita o sistema
    // a um processo só: terminais diferentes podem rodar processos
    // diferentes ao mesmo tempo, cada um com seu próprio address
    // space. running_process fica NULL quando nenhum programa está
    // em execução NESTE terminal específico. Enquanto não-NULL,
    // term_update_running_process() (chamada a cada frame do loop do
    // terminal) drena a saída do processo (task_stdout_read) e a
    // imprime como texto normal, e detecta o momento em que o
    // processo chama sys_exit (has_exited) para mostrar a mensagem
    // final e liberar este slot para um novo "run".
    task_t*  running_process;
    // Linha parcial (ainda sem '\n') acumulada entre um dreno e
    // outro — sys_write pode entregar os bytes em pedaços que não
    // batem exatamente com o fim de uma linha.
    char     running_line_buf[TERM_COLS + 1];
    int      running_line_len;
};

static void term_scroll(TermState* t) {
    for (int i = 0; i < TERM_HIST - 1; i++)
        kmemcpy(t->lines[i], t->lines[i+1], TERM_COLS + 1);
    kmemset(t->lines[TERM_HIST - 1], 0, TERM_COLS + 1);
    if (t->num_lines > 0) t->num_lines--;
}

static void term_newline(TermState* t) {
    t->scroll_offset = 0;
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
    term_println(t, tr(STR_TERM_HELP_HEADER));
    term_println(t, tr(STR_TERM_HELP_HELP));
    term_println(t, tr(STR_TERM_HELP_CLEAR));
    term_println(t, tr(STR_TERM_HELP_ECHO));
    term_println(t, tr(STR_TERM_HELP_ABOUT));
    term_println(t, tr(STR_TERM_HELP_DATE));
    term_println(t, tr(STR_TERM_HELP_MEM));
    term_println(t, tr(STR_TERM_HELP_REBOOT));
    term_println(t, tr(STR_TERM_HELP_LANG));
    term_println(t, "");
    term_println(t, tr(STR_TERM_HELP_FS_HEADER));
    term_println(t, tr(STR_TERM_HELP_PWD));
    term_println(t, tr(STR_TERM_HELP_LS));
    term_println(t, tr(STR_TERM_HELP_CD));
    term_println(t, tr(STR_TERM_HELP_MKDIR));
    term_println(t, tr(STR_TERM_HELP_TOUCH));
    term_println(t, tr(STR_TERM_HELP_CAT));
    term_println(t, tr(STR_TERM_HELP_WRITE));
    term_println(t, tr(STR_TERM_HELP_APPEND));
    term_println(t, tr(STR_TERM_HELP_RM));
    term_println(t, tr(STR_TERM_HELP_STAT));
    term_println(t, tr(STR_TERM_HELP_EDIT));
}

static void cmd_about(TermState* t) {
    term_println(t, "");
    term_println(t, tr(STR_TERM_ABOUT_TITLE));
    term_println(t, tr(STR_TERM_ABOUT_ARCH));
    term_println(t, tr(STR_TERM_ABOUT_BOOT));
    term_println(t, tr(STR_TERM_ABOUT_VIDEO));
    term_println(t, tr(STR_TERM_ABOUT_GUI));
    term_println(t, tr(STR_TERM_ABOUT_INPUT));
    term_println(t, tr(STR_TERM_ABOUT_FS));
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
    kstrcpy(line, tr(STR_TERM_DATE_LABEL));
    kstrcat(line, date_buf);
    kstrcat(line, tr(STR_TERM_TIME_LABEL));
    kstrcat(line, time_buf);
    term_println(t, line);
}

static void cmd_mem(TermState* t) {
    term_println(t, tr(STR_TERM_MEM_HEAP));
    term_println(t, tr(STR_TERM_MEM_SHADOW));
    term_println(t, tr(STR_TERM_MEM_BGCACHE));
    term_println(t, tr(STR_TERM_MEM_VFS));
}

static void cmd_reboot(TermState* t) {
    (void)t;
    vfs_sync();
    outb(0x64, 0xFE);
    while(1) __asm__("hlt");
}

static void cmd_lang(TermState* t, const char* arg) {
    while (*arg == ' ') arg++;
    if (kstrcmp(arg, "pt") == 0) {
        lang_set(LANG_PT);
        term_println(t, tr(STR_TERM_LANG_SET_PT));
    } else if (kstrcmp(arg, "en") == 0) {
        lang_set(LANG_EN);
        term_println(t, tr(STR_TERM_LANG_SET_EN));
    } else {
        term_println(t, tr(STR_TERM_LANG_USAGE));
    }
}

static void cmd_edit(const char* name) {
    uint32_t sw = fb_width(), sh = fb_height();
    editor_create((int32_t)(sw/2 - 300), (int32_t)(sh/2 - 200), name);
}

static void cmd_pwd(TermState* t) {
    char buf[512];
    vfs_path_of(vfs_cwd(), buf, sizeof(buf));
    term_println(t, buf);
}

static void cmd_ls(TermState* t, const char* path_arg) {
    VfsNode* dir;
    if (path_arg && path_arg[0]) {
        dir = vfs_resolve(vfs_cwd(), path_arg);
        if (!dir) { term_println(t, tr(STR_TERM_LS_DIR_NOT_FOUND)); return; }
        if (dir->type != VFS_DIR) { term_println(t, tr(STR_TERM_LS_NOT_A_DIR)); return; }
    } else {
        dir = vfs_cwd();
    }
    if (dir->child_count == 0) { term_println(t, tr(STR_TERM_LS_EMPTY)); return; }

    for (uint32_t i = 0; i < dir->child_count; i++) {
        VfsNode* child = dir->children[i];
        char line[TERM_COLS + 1];
        kmemset(line, 0, sizeof(line));
        if (child->type == VFS_DIR) kstrcpy(line, "  [DIR]  ");
        else                        kstrcpy(line, "  [ARQ]  ");
        // kstrncat (com limite) em vez de kstrcat: child->name tem
        // tamanho imprevisível e `line` é um buffer fixo de
        // TERM_COLS+1 — um nome de arquivo longo estouraria a pilha
        // aqui. O resultado é truncado na largura do terminal, que é
        // exatamente o comportamento desejado para uma listagem.
        kstrncat(line, child->name, sizeof(line));
        if (child->type == VFS_FILE) {
            kstrncat(line, "  (", sizeof(line));
            char sz[16];
            kitoa((int64_t)child->size, sz);
            kstrncat(line, sz, sizeof(line));
            kstrncat(line, " B)", sizeof(line));
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
    if (!target) { term_println(t, tr(STR_TERM_CD_NOT_FOUND)); return; }
    if (target->type != VFS_DIR) { term_println(t, tr(STR_TERM_CD_NOT_A_DIR)); return; }
    vfs_set_cwd(target);
}

static void cmd_mkdir(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_MKDIR_NEED_NAME)); return; }
    VfsNode* n = vfs_mkdir(vfs_cwd(), name);
    if (!n) term_println(t, tr(STR_TERM_MKDIR_FAILED));
}

static void cmd_touch(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_TOUCH_NEED_NAME)); return; }
    VfsNode* n = vfs_touch(vfs_cwd(), name);
    if (!n) term_println(t, tr(STR_TERM_TOUCH_FAILED));
}

// ---- run <arquivo.hexec> ----
// Comando de teste da Fase 2 (loader de executáveis + processos
// de usuário reais): carrega um arquivo HEXEC do VFS e cria um
// processo para rodá-lo. O processo roda de forma assíncrona (o
// scheduler decide quando escaloná-lo) — este comando só dispara a
// criação, não espera o processo terminar nem mostra a saída dele
// aqui no terminal (a saída de sys_write vai direto para o
// framebuffer por enquanto, ver comentário em syscall.c sobre essa
// limitação atual de não haver ainda uma "janela" dedicada por
// processo). Só um processo de usuário pode existir por vez nesta
// fase — ver comentário em hexec.h sobre por quê.
// ---- Drena a saída de um processo rodando "dentro" deste terminal ----
//
// Chamada a cada frame do loop principal do terminal (ver o loop em
// terminal_update/terminal_draw mais abaixo). Lê os bytes disponíveis
// em t->running_process->stdout_ring (task_stdout_read, em task.c) e
// os imprime como texto normal, quebrando em linhas nos '\n' — bytes
// que ainda não formam uma linha completa ficam acumulados em
// running_line_buf até o próximo dreno. Quando o processo sinaliza
// has_exited, imprime a mensagem final com o código de saída e limpa
// running_process (liberando este terminal para rodar outro "run" —
// a task em si só é reciclada de verdade mais tarde pelo reaper
// global, ver task_reap_zombies em gui/screens/desktop.cpp).
static void term_update_running_process(TermState* t) {
    task_t* proc = t->running_process;
    if (!proc) return;

    uint8_t chunk[64];
    uint64_t n;
    while ((n = task_stdout_read(proc, chunk, sizeof(chunk))) > 0) {
        for (uint64_t i = 0; i < n; i++) {
            char c = (char)chunk[i];
            if (c == '\n' || t->running_line_len >= TERM_COLS) {
                t->running_line_buf[t->running_line_len] = '\0';
                term_println(t, t->running_line_buf);
                t->running_line_len = 0;
            } else {
                t->running_line_buf[t->running_line_len++] = c;
            }
        }
    }

    if (proc->has_exited) {
        // Imprime qualquer resto de linha parcial que não terminou
        // com '\n' antes do processo sair — senão esses bytes finais
        // ficariam silenciosamente perdidos.
        if (t->running_line_len > 0) {
            t->running_line_buf[t->running_line_len] = '\0';
            term_println(t, t->running_line_buf);
            t->running_line_len = 0;
        }
        char msg[64] = "[processo encerrado, codigo=";
        char numbuf[24];
        kuitoa(proc->exit_code, numbuf);
        kstrcat(msg, numbuf);
        kstrcat(msg, "]");
        term_println(t, msg);

        // Este terminal não é mais "dono" do processo — a task ainda
        // existe (TASK_ZOMBIE) até o reaper global rodar, mas daqui
        // para frente ninguém mais escreve em stdout_owner por conta
        // deste terminal, então soltamos a referência.
        proc->stdout_owner = NULL;
        t->running_process = nullptr;
    }
}

static void cmd_run(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "uso: run <arquivo.hexec>"); return; }
    if (t->running_process) {
        term_println(t, "run: ja ha um processo rodando neste terminal");
        return;
    }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, "run: arquivo nao encontrado"); return; }
    if (node->type == VFS_DIR) { term_println(t, "run: e um diretorio"); return; }

    task_t* proc = process_create(name, node);
    if (!proc) {
        term_println(t, "run: falha ao criar processo (HEXEC invalido, sem memoria,");
        term_println(t, "     tabela de tasks cheia, ou ja existe um processo de");
        term_println(t, "     usuario rodando nesta fase — ver hexec.h)");
        return;
    }
    proc->stdout_owner   = t;
    t->running_process   = proc;
    t->running_line_len  = 0;
    term_println(t, "run: processo iniciado");
}

static void cmd_cat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_CAT_NEED_NAME)); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, tr(STR_TERM_CAT_NOT_FOUND)); return; }
    if (node->type == VFS_DIR) { term_println(t, tr(STR_TERM_CAT_IS_DIR)); return; }
    if (!node->data || node->size == 0) { term_println(t, tr(STR_TERM_CAT_EMPTY)); return; }
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
    while (*rest == ' ') rest++;
    if (!*rest) { term_println(t, tr(STR_TERM_WRITE_USAGE)); return; }

    char fname[VFS_NAME_MAX];
    int fi = 0;
    while (*rest && *rest != ' ' && fi < (int)(VFS_NAME_MAX) - 1) {
        fname[fi++] = *rest++;
    }
    fname[fi] = 0;

    while (*rest == ' ') rest++;

    if (!fname[0]) { term_println(t, tr(STR_TERM_WRITE_EMPTY_NAME)); return; }

    VfsNode* node = vfs_resolve(vfs_cwd(), fname);
    if (!node) node = vfs_touch(vfs_cwd(), fname);
    if (!node || node->type != VFS_FILE) { term_println(t, tr(STR_TERM_WRITE_NOT_A_FILE)); return; }

    vfs_write(node, rest);
    vfs_append(node, "\n");
    term_println(t, tr(STR_TERM_WRITE_DONE));
}

static void cmd_append(TermState* t, const char* rest) {
    while (*rest == ' ') rest++;
    if (!*rest) { term_println(t, tr(STR_TERM_APPEND_USAGE)); return; }

    char fname[VFS_NAME_MAX];
    int fi = 0;
    while (*rest && *rest != ' ' && fi < (int)(VFS_NAME_MAX) - 1) {
        fname[fi++] = *rest++;
    }
    fname[fi] = 0;

    while (*rest == ' ') rest++;

    VfsNode* node = vfs_resolve(vfs_cwd(), fname);
    if (!node) node = vfs_touch(vfs_cwd(), fname);
    if (!node || node->type != VFS_FILE) { term_println(t, tr(STR_TERM_APPEND_NOT_A_FILE)); return; }

    vfs_append(node, rest);
    vfs_append(node, "\n");
    term_println(t, tr(STR_TERM_APPEND_DONE));
}

static void cmd_rm(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_RM_NEED_NAME)); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, tr(STR_TERM_RM_NOT_FOUND)); return; }
    if (node == vfs_root()) { term_println(t, tr(STR_TERM_RM_CANT_ROOT)); return; }
    if (node == vfs_cwd()) { term_println(t, tr(STR_TERM_RM_CANT_CWD)); return; }
    vfs_rm(node);
    term_println(t, tr(STR_TERM_RM_DONE));
}

static void cmd_stat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_STAT_NEED_NAME)); return; }
    VfsNode* node = vfs_resolve(vfs_cwd(), name);
    if (!node) { term_println(t, tr(STR_TERM_STAT_NOT_FOUND)); return; }
    char buf[TERM_COLS + 1];
    // kstrncat em todos os pontos abaixo (não kstrcat): nome de
    // arquivo e, principalmente, o caminho absoluto (path[512]) têm
    // tamanho imprevisível e `buf` tem só TERM_COLS+1 bytes — a
    // concatenação sem limite era um estouro de pilha esperando
    // acontecer, da mesma família do bug que travava o sistema ao
    // executar um .hexec pelo explorador de arquivos.
    kstrcpy(buf, tr(STR_TERM_STAT_NAME)); kstrncat(buf, node->name, sizeof(buf));
    term_println(t, buf);
    kstrcpy(buf, tr(STR_TERM_STAT_TYPE));
    kstrncat(buf, node->type == VFS_DIR ? tr(STR_TERM_STAT_TYPE_DIR) : tr(STR_TERM_STAT_TYPE_FILE), sizeof(buf));
    term_println(t, buf);
    if (node->type == VFS_FILE) {
        char sz[24]; kitoa((int64_t)node->size, sz);
        kstrcpy(buf, tr(STR_TERM_STAT_SIZE)); kstrncat(buf, sz, sizeof(buf)); kstrncat(buf, " bytes", sizeof(buf));
        term_println(t, buf);
    } else {
        char cnt[24]; kitoa((int64_t)node->child_count, cnt);
        kstrcpy(buf, tr(STR_TERM_STAT_ITEMS)); kstrncat(buf, cnt, sizeof(buf));
        term_println(t, buf);
    }
    char path[512];
    vfs_path_of(node, path, sizeof(path));
    kstrcpy(buf, tr(STR_TERM_STAT_PATH)); kstrncat(buf, path, sizeof(buf));
    term_println(t, buf);
}

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

static void term_execute(TermState* t, const char* input) {
    char cmd[32];
    const char* args;

    parse_command(input, cmd, sizeof(cmd), &args);
    if (cmd[0] == '\0') return;

    if (t->cmd_hist_count < TERM_CMD_HISTORY) {
        kstrcpy(t->cmd_history[t->cmd_hist_count++], input);
    } else {
        for (int i = 0; i < TERM_CMD_HISTORY - 1; i++)
            kmemcpy(t->cmd_history[i], t->cmd_history[i+1], TERM_BUF_SIZE);
        kstrcpy(t->cmd_history[TERM_CMD_HISTORY - 1], input);
    }
    t->cmd_hist_pos = -1;

    if (kstrcmp(cmd, "help") == 0) {
        cmd_help(t);
    } else if (kstrcmp(cmd, "clear") == 0) {
        for (int i = 0; i < TERM_HIST; i++) kmemset(t->lines[i], 0, TERM_COLS+1);
        t->num_lines = 1;
        t->scroll_offset = 0;
    } else if (kstrcmp(cmd, "echo") == 0) {
        term_println(t, args);
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
    } else if (kstrcmp(cmd, "run") == 0) {
        cmd_run(t, args);
    } else if (kstrcmp(cmd, "write") == 0) {
        cmd_write(t, args);
    } else if (kstrcmp(cmd, "append") == 0) {
        cmd_append(t, args);
    } else if (kstrcmp(cmd, "rm") == 0) {
        cmd_rm(t, args);
    } else if (kstrcmp(cmd, "stat") == 0) {
        cmd_stat(t, args);
    } else if (kstrcmp(cmd, "lang") == 0) {
        cmd_lang(t, args);
    } else if (kstrcmp(cmd, "edit") == 0) {
        cmd_edit(args);
    } else {
        char buf[TERM_COLS + 1];
        kstrcpy(buf, tr(STR_TERM_CMD_NOT_FOUND));
        // kstrncat: `cmd` vem direto da entrada do usuário, que pode
        // ter até TERM_BUF_SIZE (256) bytes — não cabe garantidamente
        // neste buffer de TERM_COLS+1.
        kstrncat(buf, cmd, sizeof(buf));
        term_println(t, buf);
        term_println(t, tr(STR_TERM_CMD_NOT_FOUND_HINT));
    }
}

// ============================================================
//  TerminalWindow — subclasse de Window. TermState era alocado
//  via kzalloc() e guardado em win->content (void*); agora é
//  membro direto da classe (m_state), sem alocação separada.
// ============================================================
class TerminalWindow : public Window {
public:
    TermState m_state;
    ScrollBar m_scrollbar;

    TerminalWindow(int32_t x, int32_t y, uint32_t w, uint32_t h, const char* title)
                : Window(x, y, w, h, title, WinType::Terminal),
                      m_scrollbar(0, 0, ScrollBar::DEFAULT_WIDTH, 1)
    {
        kmemset(&m_state, 0, sizeof(TermState));
        m_state.num_lines      = 1;
        m_state.cursor_visible = true;
        m_state.last_blink     = 0;
        m_state.cmd_hist_pos   = -1;

        term_println(&m_state, tr(STR_TERM_WELCOME_1));
        term_println(&m_state, tr(STR_TERM_WELCOME_2));
        term_println(&m_state, "");
    }

    ~TerminalWindow() override {
        // Se um processo de usuário ainda estiver rodando "dentro"
        // deste terminal quando a janela é fechada, ele fica sem
        // dono de stdout — desassociamos aqui para não deixar
        // proc->stdout_owner como um ponteiro pendurado apontando
        // para esta TermState prestes a ser destruída (use-after-free
        // na próxima vez que sys_write tentasse gravar, ou pior, na
        // próxima vez que outro terminal também fosse fechado e essa
        // região de memória fosse reaproveitada para outra coisa). O
        // processo em si continua rodando até terminar sozinho ou ser
        // colhido pelo reaper — esta fase ainda não tem uma forma de
        // "matar" um processo de fora, isso fica para depois.
        if (m_state.running_process) {
            m_state.running_process->stdout_owner = nullptr;
            m_state.running_process = nullptr;
        }
    }

    void draw(int32_t ox, int32_t oy) override {
        Window::draw(ox, oy);
        if (!active || minimized) return;

        TermState* t = &m_state;
        term_update_running_process(t); // drena stdout do processo,
                                          // se houver um rodando —
                                          // feito aqui porque draw()
                                          // já roda a cada frame do
                                          // desktop, dando ao processo
                                          // uma cadência de atualização
                                          // visível sem precisar de um
                                          // timer dedicado.
        Rect content = content_area_absolute();
        int bx = content.x + PAD;
        int by = content.y + PAD;
        int bw = (int)content.w - PAD * 2 - (int)ScrollBar::DEFAULT_WIDTH;
        int bh = (int)content.h - PAD * 2;

        int visible_rows = bh / FONT_H;
        int output_rows = visible_rows - 1;
        if (output_rows < 1) output_rows = 1;
        if (visible_rows < 1) visible_rows = 1;
        int max_offset = t->num_lines > output_rows ? t->num_lines - output_rows : 0;
        if (t->scroll_offset > max_offset) t->scroll_offset = max_offset;
        int track_x = content.x + (int)content.w - PAD - (int)ScrollBar::DEFAULT_WIDTH;
        int track_h = output_rows * FONT_H;
        m_scrollbar.bounds = Rect{track_x, by, ScrollBar::DEFAULT_WIDTH, (uint32_t)track_h};
        m_scrollbar.set_range(t->num_lines, output_rows);
        // O terminal conta a partir do fim; o ScrollBar conta a partir do topo.
        m_scrollbar.set_value(max_offset - t->scroll_offset);
        int start_line = t->num_lines - output_rows - t->scroll_offset;
        if (start_line < 0) start_line = 0;
        for (int r = 0; r < output_rows; r++) {
            int line_idx = start_line + r;
            int ry = by + r * FONT_H;
            if (line_idx < 0 || line_idx >= TERM_HIST) continue;
            char* text = t->lines[line_idx];

            fb_fill_rect((uint32_t)bx, (uint32_t)ry, (uint32_t)bw, FONT_H, COLOR_TERM_BG);
            if (!text[0]) continue;

            if (kstrncmp(text, "haos:", 5) == 0) {
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

        int input_y = by + output_rows * FONT_H;
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

        m_scrollbar.draw(0, 0);
    }

    EventResult on_event(const WidgetEvent& ev) override {
        TermState* t = &m_state;
        Rect content = content_area_absolute();
        int output_rows = ((int)content.h - PAD * 2) / FONT_H - 1;
        if (output_rows < 1) output_rows = 1;
        int track_x = content.x + (int)content.w - PAD - (int)ScrollBar::DEFAULT_WIDTH;
        int track_y = content.y + PAD;
        int track_h = output_rows * FONT_H;

        if (ev.type == EventType::MouseDown || ev.type == EventType::MouseDrag ||
            ev.type == EventType::MouseScroll) {
            int x = ev.x;
            int y = ev.y;
            bool on_bar = x >= track_x && x < track_x + (int)ScrollBar::DEFAULT_WIDTH &&
                          y >= track_y && y < track_y + track_h;
            if (on_bar || ev.type == EventType::MouseDrag) {
                WidgetEvent bar_ev = ev;
                bar_ev.x = x - track_x;
                bar_ev.y = y - track_y;
                EventResult result = m_scrollbar.on_event(bar_ev);
                if (result == EventResult::Handled) {
                    int max_offset = t->num_lines > output_rows ? t->num_lines - output_rows : 0;
                    t->scroll_offset = max_offset - m_scrollbar.value();
                    return result;
                }
                if (on_bar) return EventResult::Handled;
            }
            if (ev.type == EventType::MouseScroll) {
                m_scrollbar.set_value(m_scrollbar.value() - ev.scroll);
                int max_offset = t->num_lines > output_rows ? t->num_lines - output_rows : 0;
                t->scroll_offset = max_offset - m_scrollbar.value();
                return EventResult::Handled;
            }
        }
        if (ev.type == EventType::MouseUp) {
            WidgetEvent bar_ev = ev;
            bar_ev.x = ev.x - track_x;
            bar_ev.y = ev.y - track_y;
            m_scrollbar.on_event(bar_ev);
            return EventResult::Handled;
        }
        if (ev.type != EventType::KeyDown) return EventResult::Ignored;

        uint8_t c = ev.key;

        int max_offset = t->num_lines > output_rows ? t->num_lines - output_rows : 0;
        if (c == KEY_PAGE_UP) {
            t->scroll_offset += output_rows;
            if (t->scroll_offset > max_offset) t->scroll_offset = max_offset;
            return EventResult::Handled;
        } else if (c == KEY_PAGE_DOWN) {
            t->scroll_offset -= output_rows;
            if (t->scroll_offset < 0) t->scroll_offset = 0;
            return EventResult::Handled;
        } else if (c == KEY_HOME) {
            t->scroll_offset = max_offset;
            return EventResult::Handled;
        } else if (c == KEY_END) {
            t->scroll_offset = 0;
            return EventResult::Handled;
        } else if (c == '\n' || c == '\r') {
            char prompt[PROMPT_MAX + 1];
            term_build_prompt(prompt, sizeof(prompt));
            char echo_line[TERM_COLS + PROMPT_MAX + 2];
            kstrcpy(echo_line, prompt);
            // Este buffer está dimensionado para caber prompt+input,
            // mas usamos a versão com limite por consistência com o
            // resto do arquivo e como defesa em profundidade caso
            // PROMPT_MAX ou TERM_BUF_SIZE mudem no futuro sem que
            // alguém revise este cálculo.
            kstrncat(echo_line, t->input, sizeof(echo_line));
            term_println(t, echo_line);
            term_execute(t, t->input);
            kmemset(t->input, 0, TERM_BUF_SIZE);
            t->input_len = 0;
            t->cmd_hist_pos = -1;

        } else if (c == '\b' || c == 0x08) {
            if (t->input_len > 0) {
                t->input_len--;
                t->input[t->input_len] = 0;
            }

        } else if (c == KEY_UP) {
            if (t->cmd_hist_count == 0) return EventResult::Handled;
            if (t->cmd_hist_pos < 0) t->cmd_hist_pos = t->cmd_hist_count - 1;
            else if (t->cmd_hist_pos > 0) t->cmd_hist_pos--;
            kstrcpy(t->input, t->cmd_history[t->cmd_hist_pos]);
            t->input_len = (int)kstrlen(t->input);

        } else if (c == KEY_DOWN) {
            if (t->cmd_hist_pos < 0) return EventResult::Handled;
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
        return EventResult::Handled;
    }
};

// ---- Criação ----

Window* terminal_create(int32_t x, int32_t y) {
    uint32_t w = FONT_W * TERM_COLS + (uint32_t)(Window::BORDER*2 + PAD*2 + ScrollBar::DEFAULT_WIDTH);
    uint32_t h = FONT_H * (TERM_ROWS + 1) + (uint32_t)(Window::BORDER + Window::TITLE_BAR_H + 1 + PAD*2);

    TerminalWindow* win = new TerminalWindow(x, y, w, h, "Terminal -- HAOS Shell v1.2");
    if (!win) {
        // Heap esgotada/fragmentada demais para alocar outro
        // TerminalWindow. Com -fno-exceptions, `new` devolve nullptr
        // em vez de lançar std::bad_alloc — se não checarmos aqui, o
        // construtor rodaria escrevendo no endereço 0 e derrubaria o
        // sistema inteiro. Falhar silenciosamente (sem abrir a janela)
        // é muito melhor que travar tudo; quem chama propaga o erro.
        return nullptr;
    }
    if (!wm_register(win)) {
        // Sem slot livre no WM (MAX_WINDOWS janelas já abertas).
        // ANTES desta checagem, a janela recém-criada com `new` ficava
        // órfã: nunca registrada, nunca deletada, e ainda assim
        // devolvida ao chamador como se tivesse dado certo — vazando
        // memória e, pior, virando dona do stdout de um processo que
        // jamais seria desenhado nem atualizado (o que travava o
        // processo como zumbi para sempre, já que o reaper não colhe
        // uma task cujo stdout_owner ainda é não-nulo).
        delete win;
        return nullptr;
    }
    return win;
}

Window* terminal_create_and_run(int32_t x, int32_t y, const char* hexec_path) {
    Window* win = terminal_create(x, y);
    if (!win) return nullptr; // sem slot de janela livre — ver terminal_create
    TerminalWindow* tw = static_cast<TerminalWindow*>(win);
    // Chama cmd_run diretamente (mesma função usada quando o usuário
    // digita "run <arquivo>" no prompt) em vez de duplicar a lógica
    // de criação de processo aqui — o explorador de arquivos delega
    // toda a execução ao terminal, como pedido: "rodar" um .hexec
    // pelo explorador não deveria ter um caminho de execução
    // diferente do comando run, só uma forma mais rápida de chegar
    // até ele.
    // Eco visual do comando disparado, para deixar claro ao usuário
    // que isto equivale a ter digitado o comando manualmente.
    //
    // Usa kstrncat (com limite) e não kstrcat: `hexec_path` vem do
    // explorador de arquivos como um CAMINHO ABSOLUTO de até 512
    // bytes (ver file_manager.cpp), enquanto este buffer tem só
    // TERM_COLS+1 (81) bytes. Concatenar sem limite aqui estourava a
    // pilha da task da GUI — causando travamentos erráticos ao
    // executar um .hexec pelo explorador (às vezes uma pausa longa,
    // às vezes congelamento total, dependendo do que fosse corrompido
    // na pilha). Pelo terminal o bug não aparecia, porque ali o
    // usuário digita um caminho curto e relativo.
    char echo[TERM_COLS + 1];
    kstrcpy(echo, "> run ");
    kstrncat(echo, hexec_path, sizeof(echo));
    term_println(&tw->m_state, echo);

    cmd_run(&tw->m_state, hexec_path);
    return win;
}

void terminal_tick(Window* win, uint64_t ticks) {
    if (!win) return;
    TerminalWindow* tw = static_cast<TerminalWindow*>(win);
    TermState* t = &tw->m_state;
    if (ticks - t->last_blink > 50) {
        t->cursor_visible = !t->cursor_visible;
        t->last_blink = ticks;
    }
}
