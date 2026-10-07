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
#include "../../kernel/user.h"
#include "../../fs/perm.h"

// Ver o comentário grande em TASK_LAYOUT_GUARD (kernel/task.h): trava
// em tempo de compilação para garantir que este compilador C++
// concorda com o compilador C do kernel (kernel/task.c e outros)
// sobre o tamanho de task_t — este arquivo é o único .cpp do projeto
// que de fato lê/escreve campos de task_t (proc->stdout_owner,
// proc->has_exited, etc.), então é o ponto certo para checar o lado
// C++ dessa fronteira.
TASK_LAYOUT_GUARD(terminal_cpp);

#define PAD 8
#define TERM_CMD_HISTORY 20
#define PROMPT_MAX 40

// Etapas do prompt de senha (passwd / adduser) — ver term_password_line().
#define PW_NONE    0
#define PW_CURRENT 1   // senha atual (só ao trocar a PRÓPRIA senha, se tiver uma)
#define PW_NEW     2
#define PW_CONFIRM 3
#define TERM_MAX_BG_PROCESSES 4  // quantos processos em background
                                   // ("run prog.hexec &") um mesmo
                                   // terminal pode acompanhar ao
                                   // mesmo tempo — ver bg_processes
                                   // em TermState.

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

    // ---- Prompt de senha (passwd / adduser) ----
    // Enquanto pw_stage != PW_NONE, o teclado edita uma linha MASCARADA
    // (só asteriscos na tela), que não vai para o histórico de comandos.
    int      pw_stage;
    bool     pw_create;                        // true = adduser, false = passwd
    bool     pw_admin;                         // adduser ... admin
    uint32_t pw_target_uid;
    char     pw_target_name[USER_NAME_MAX];
    char     pw_first[PASSWORD_MAX + 1];       // 1ª digitação (até confirmar)

    // ---- Processo de usuário rodando "dentro" deste terminal ----
    // Só um processo "em foco" por terminal — cada terminal mostra
    // a saída de um programa de cada vez e só um recebe o teclado.
    // Desde a Fase 3 (isolamento de espaço de endereço, ver
    // hexec.h/paging.h), isso NÃO limita o sistema a um processo só:
    // terminais diferentes podem rodar processos diferentes ao mesmo
    // tempo, cada um com seu próprio address space. running_process
    // fica NULL quando nenhum programa está em execução EM FOCO
    // NESTE terminal específico. Enquanto não-NULL,
    // term_update_running_process() (chamada a cada frame do loop do
    // terminal) drena a saída do processo (task_stdout_read) e a
    // imprime como texto normal, e detecta o momento em que o
    // processo chama sys_exit (has_exited) para mostrar a mensagem
    // final e liberar este slot para um novo "run". O teclado (ver
    // on_event) só é desviado para o stdin deste processo — nunca
    // dos de bg_processes, abaixo — então só um programa por vez
    // pode receber input.
    task_t*  running_process;
    // Linha parcial (ainda sem '\n') acumulada entre um dreno e
    // outro — sys_write pode entregar os bytes em pedaços que não
    // batem exatamente com o fim de uma linha.
    char     running_line_buf[TERM_COLS + 1];
    int      running_line_len;

    // ---- Processos em BACKGROUND ("run prog.hexec &") ----
    //
    // Diferente de running_process (só 1, recebe teclado, "ocupa" o
    // terminal), um processo em background continua com
    // stdout_owner apontando para este terminal (a saída dele ainda
    // aparece aqui, prefixada com o nome — ver term_update_bg_processes),
    // mas NÃO bloqueia o prompt: o usuário pode digitar novos
    // comandos enquanto ele roda. Também não recebe nenhum stdin —
    // um processo em bg não tem como ler teclado (não haveria como
    // saber para qual dos vários processos em bg uma tecla deveria
    // ir); um programa que precisa de input do usuário deve rodar em
    // foreground (sem '&'). Array de tamanho fixo pequeno — não é
    // esperado que um usuário rode dezenas de processos em bg ao
    // mesmo tempo num terminal só; ver TERM_MAX_BG_PROCESSES.
    task_t*  bg_processes[TERM_MAX_BG_PROCESSES];
    char     bg_line_buf[TERM_MAX_BG_PROCESSES][TERM_COLS + 1];
    int      bg_line_len[TERM_MAX_BG_PROCESSES];
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
    term_println(t, "");
    term_println(t, tr(STR_TERM_HELP_USERS_HEADER));
    term_println(t, tr(STR_TERM_HELP_WHOAMI));
    term_println(t, tr(STR_TERM_HELP_ID));
    term_println(t, tr(STR_TERM_HELP_USERS));
    term_println(t, tr(STR_TERM_HELP_ADDUSER));
    term_println(t, tr(STR_TERM_HELP_DELUSER));
    term_println(t, tr(STR_TERM_HELP_PASSWD));
    term_println(t, tr(STR_TERM_HELP_CHMOD));
    term_println(t, tr(STR_TERM_HELP_CHOWN));
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

// Acrescenta `s` a `line` e completa com espaços até `width` colunas.
static void append_padded(char* line, size_t cap, const char* s, size_t width) {
    kstrncat(line, s, cap);
    size_t n = kstrlen(s);
    while (n < width && kstrlen(line) + 1 < cap) { kstrncat(line, " ", cap); n++; }
}

// Nome do dono para exibição ("ana", "root"; ou o número se a conta sumiu).
static void owner_name(uint32_t uid, char* out, size_t cap) {
    const UserEntry* u = user_find_uid(uid);
    if (u) { kstrncpy(out, u->name, cap - 1); out[cap - 1] = '\0'; }
    else   { char num[16]; kuitoa(uid, num); kstrncpy(out, num, cap - 1); out[cap - 1] = '\0'; }
}

// ls [-l] [caminho] — com -l mostra permissões e dono (formato do Unix).
static void cmd_ls(TermState* t, const char* path_arg) {
    bool long_fmt = false;
    while (path_arg && *path_arg == ' ') path_arg++;
    if (path_arg && path_arg[0] == '-' && path_arg[1] == 'l' &&
        (path_arg[2] == '\0' || path_arg[2] == ' ')) {
        long_fmt = true;
        path_arg += 2;
        while (*path_arg == ' ') path_arg++;
    }

    Cred cred = session_cred();
    VfsNode* dir = nullptr;
    if (path_arg && path_arg[0]) {
        int rr = vfs_resolve_as(&cred, vfs_cwd(), path_arg, &dir);
        if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
        if (rr != PERM_OK || !dir) { term_println(t, tr(STR_TERM_LS_DIR_NOT_FOUND)); return; }
        if (dir->type != VFS_DIR) { term_println(t, tr(STR_TERM_LS_NOT_A_DIR)); return; }
    } else {
        dir = vfs_cwd();
    }
    if (!perm_allows(dir, &cred, PERM_R)) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (dir->child_count == 0) { term_println(t, tr(STR_TERM_LS_EMPTY)); return; }

    for (uint32_t i = 0; i < dir->child_count; i++) {
        VfsNode* child = dir->children[i];
        char line[TERM_COLS + 1];
        kmemset(line, 0, sizeof(line));
        if (long_fmt) {
            char pm[11];
            perm_format(child, pm);
            char ow[USER_NAME_MAX];
            owner_name(child->uid, ow, sizeof(ow));
            kstrcpy(line, " ");
            kstrncat(line, pm, sizeof(line));
            kstrncat(line, " ", sizeof(line));
            append_padded(line, sizeof(line), ow, 9);
        } else if (child->type == VFS_DIR) {
            kstrcpy(line, "  [DIR]  ");
        } else {
            kstrcpy(line, "  [ARQ]  ");
        }
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
    Cred cred = session_cred();
    if (!path_arg || !path_arg[0]) {
        // "cd" sem argumento: vai para a pasta pessoal do usuário logado.
        VfsNode* home = nullptr;
        if (vfs_resolve_as(&cred, vfs_root(), session_home(), &home) != PERM_OK || !home)
            home = vfs_root();
        vfs_set_cwd(home);
        return;
    }
    VfsNode* target = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), path_arg, &target);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !target) { term_println(t, tr(STR_TERM_CD_NOT_FOUND)); return; }
    if (target->type != VFS_DIR) { term_println(t, tr(STR_TERM_CD_NOT_A_DIR)); return; }
    if (!perm_allows(target, &cred, PERM_X)) { term_println(t, tr(STR_PERM_DENIED)); return; }
    vfs_set_cwd(target);
}

static void cmd_mkdir(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_MKDIR_NEED_NAME)); return; }
    Cred cred = session_cred();
    int err = PERM_OK;
    VfsNode* n = vfs_mkdir_as(&cred, vfs_cwd(), name, &err);
    if (!n) term_println(t, err == PERM_EACCES ? tr(STR_PERM_DENIED) : tr(STR_TERM_MKDIR_FAILED));
}

static void cmd_touch(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_TOUCH_NEED_NAME)); return; }
    Cred cred = session_cred();
    int err = PERM_OK;
    VfsNode* n = vfs_touch_as(&cred, vfs_cwd(), name, &err);
    if (!n) term_println(t, err == PERM_EACCES ? tr(STR_PERM_DENIED) : tr(STR_TERM_TOUCH_FAILED));
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
        // exit_code == (uint64_t)-1 é a convenção de task_kill() para
        // "morto à força" (Ctrl+C) — mostrar isso como o número
        // 18446744073709551615 seria ilegível, então tem mensagem própria.
        if (proc->exit_code == (uint64_t)-1) {
            term_println(t, "[processo interrompido]");
        } else {
            char msg[64] = "[processo encerrado, codigo=";
            char numbuf[24];
            kuitoa(proc->exit_code, numbuf);
            kstrcat(msg, numbuf);
            kstrcat(msg, "]");
            term_println(t, msg);
        }

        // Este terminal não é mais "dono" do processo — a task ainda
        // existe (TASK_ZOMBIE) até o reaper global rodar, mas daqui
        // para frente ninguém mais escreve em stdout_owner por conta
        // deste terminal, então soltamos a referência.
        proc->stdout_owner = NULL;
        t->running_process = nullptr;
    }
}

// ---- Drena a saída de todos os processos em BACKGROUND ----
//
// Espelha term_update_running_process, mas para os slots de
// bg_processes — chamada a cada frame igual à outra. Diferenças:
// (1) cada linha impressa é PREFIXADA com "[nome]" para o usuário
// saber de qual processo em bg veio, já que várias saídas podem se
// intercalar com o que o usuário está digitando no prompt normal;
// (2) ao terminar, o slot é liberado (bg_processes[i] = nullptr)
// mas o TERMINAL em si não é afetado — running_process nem prompt
// mudam, diferente do caminho de foreground.
static void term_update_bg_processes(TermState* t) {
    for (int i = 0; i < TERM_MAX_BG_PROCESSES; i++) {
        task_t* proc = t->bg_processes[i];
        if (!proc) continue;

        uint8_t chunk[64];
        uint64_t n;
        while ((n = task_stdout_read(proc, chunk, sizeof(chunk))) > 0) {
            for (uint64_t k = 0; k < n; k++) {
                char c = (char)chunk[k];
                if (c == '\n' || t->bg_line_len[i] >= TERM_COLS) {
                    t->bg_line_buf[i][t->bg_line_len[i]] = '\0';
                    char prefixed[TERM_COLS + 1];
                    prefixed[0] = '\0';
                    kstrncat(prefixed, "[", sizeof(prefixed));
                    kstrncat(prefixed, proc->name, sizeof(prefixed));
                    kstrncat(prefixed, "] ", sizeof(prefixed));
                    kstrncat(prefixed, t->bg_line_buf[i], sizeof(prefixed));
                    term_println(t, prefixed);
                    t->bg_line_len[i] = 0;
                } else {
                    t->bg_line_buf[i][t->bg_line_len[i]++] = c;
                }
            }
        }

        if (proc->has_exited) {
            if (t->bg_line_len[i] > 0) {
                t->bg_line_buf[i][t->bg_line_len[i]] = '\0';
                char prefixed[TERM_COLS + 1];
                prefixed[0] = '\0';
                kstrncat(prefixed, "[", sizeof(prefixed));
                kstrncat(prefixed, proc->name, sizeof(prefixed));
                kstrncat(prefixed, "] ", sizeof(prefixed));
                kstrncat(prefixed, t->bg_line_buf[i], sizeof(prefixed));
                term_println(t, prefixed);
                t->bg_line_len[i] = 0;
            }
            char msg[80];
            msg[0] = '\0';
            kstrncat(msg, "[", sizeof(msg));
            kstrncat(msg, proc->name, sizeof(msg));
            if (proc->exit_code == (uint64_t)-1) {
                kstrncat(msg, "] processo em bg interrompido", sizeof(msg));
            } else {
                kstrncat(msg, "] processo em bg encerrado, codigo=", sizeof(msg));
                char numbuf[24];
                kuitoa(proc->exit_code, numbuf);
                kstrncat(msg, numbuf, sizeof(msg));
            }
            term_println(t, msg);

            proc->stdout_owner = NULL;
            t->bg_processes[i] = nullptr;
        }
    }
}

// ---- Adoção de filhos criados por sys_spawn ----
//
// Registrada em task_set_adopt_child_hook (ver task.h). `owner` é o
// stdout_owner do PAI, que sabemos ser um TermState* (é o único tipo
// que este projeto grava em stdout_owner). Procura um slot livre em
// bg_processes; se não houver, devolve false e o kernel desfaz o
// spawn.
static bool term_adopt_child(void* owner, task_t* child) {
    TermState* t = (TermState*)owner;
    if (!t || !child) return false;
    for (int i = 0; i < TERM_MAX_BG_PROCESSES; i++) {
        if (!t->bg_processes[i]) {
            t->bg_processes[i] = child;
            t->bg_line_len[i]  = 0;
            return true;
        }
    }
    return false;
}

static void cmd_run(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, "uso: run <arquivo.hexec> [args...] [&]"); return; }

    // ---- Detecta "&" no final (background) ----
    //
    // Convenção de shell Unix: um "&" como último token da linha
    // significa "rode isto em background, não espere/ocupe o
    // terminal". Copiamos `name` para um buffer local mutável
    // (`line`) porque precisamos cortar o "&" fora antes do parsing
    // normal de filename/argv abaixo — `name` original (const char*)
    // não pode ser modificado no lugar.
    char line[TERM_COLS + 1];
    kstrncpy(line, name, sizeof(line));
    line[sizeof(line) - 1] = '\0'; // kstrncpy não garante \0 se name
                                     // for >= sizeof(line) — ver
                                     // implementação em memory.cpp.

    bool background = false;
    {
        int len = (int)kstrlen(line);
        int end = len;
        while (end > 0 && line[end - 1] == ' ') end--; // ignora espaços finais
        if (end > 0 && line[end - 1] == '&') {
            // "&" precisa ser um TOKEN separado (precedido por
            // espaço ou início de linha), não parte de um nome de
            // arquivo — evita interpretar algo como "nome&estranho"
            // (improvável, mas mais correto) como pedido de bg.
            int amp_pos = end - 1;
            if (amp_pos == 0 || line[amp_pos - 1] == ' ') {
                background = true;
                line[amp_pos] = '\0';
                // Corta espaços que sobraram entre o filename/args e
                // o "&" removido.
                int trim = amp_pos;
                while (trim > 0 && line[trim - 1] == ' ') trim--;
                line[trim] = '\0';
            }
        }
    }
    name = line; // resto da função usa `name` normalmente — já sem o "&"

    if (!name[0]) { term_println(t, "uso: run <arquivo.hexec> [args...] [&]"); return; }

    task_t** target_slot = nullptr; // onde guardar o task_t* criado
    int*     target_line_len = nullptr;
    char*    target_line_buf = nullptr;

    if (background) {
        int slot = -1;
        for (int i = 0; i < TERM_MAX_BG_PROCESSES; i++) {
            if (!t->bg_processes[i]) { slot = i; break; }
        }
        if (slot < 0) {
            term_println(t, "run: limite de processos em background deste terminal atingido");
            return;
        }
        target_slot = &t->bg_processes[slot];
        target_line_len = &t->bg_line_len[slot];
        target_line_buf = t->bg_line_buf[slot];
    } else {
        if (t->running_process) {
            term_println(t, "run: ja ha um processo em foco neste terminal (use '&' para bg)");
            return;
        }
        target_slot = &t->running_process;
        target_line_len = &t->running_line_len;
        target_line_buf = t->running_line_buf;
    }

    // Separa o nome do arquivo (primeiro token) do resto da linha
    // (argumentos a repassar para o processo). `name` aqui é tudo
    // que veio depois de "run " no prompt — pode conter múltiplos
    // tokens separados por espaço, ex.: "programa.hexec arg1 arg2".
    char filename[TERM_COLS + 1];
    int fi = 0;
    while (name[fi] && name[fi] != ' ' && fi < TERM_COLS) {
        filename[fi] = name[fi];
        fi++;
    }
    filename[fi] = '\0';
    const char* rest = name + fi;
    while (*rest == ' ') rest++; // pula espaços entre o nome do
                                   // arquivo e o primeiro argumento

    // O processo roda com a identidade do usuário logado (process_create) e
    // precisa de permissão de LEITURA no arquivo, como qualquer acesso.
    Cred rcred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&rcred, vfs_cwd(), filename, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, "run: arquivo nao encontrado"); return; }
    if (node->type == VFS_DIR) { term_println(t, "run: e um diretorio"); return; }
    if (!perm_allows(node, &rcred, PERM_R)) { term_println(t, tr(STR_PERM_DENIED)); return; }

    // Monta argv_str: argv[0] é sempre o nome do arquivo (convenção
    // Unix — o próprio programa recebe seu nome como primeiro
    // argumento), seguido de cada token do restante da linha,
    // cada um terminado em '\0'.
    char argv_str[TERM_COLS + 1];
    uint64_t argv_len = 0;
    {
        uint64_t flen = kstrlen(filename);
        kmemcpy(argv_str, filename, flen);
        argv_str[flen] = '\0';
        argv_len = flen + 1;

        const char* p = rest;
        while (*p && argv_len < sizeof(argv_str) - 1) {
            uint64_t start = argv_len;
            while (*p && *p != ' ' && argv_len < sizeof(argv_str) - 1) {
                argv_str[argv_len++] = *p++;
            }
            if (argv_len > start) {
                argv_str[argv_len++] = '\0';
            }
            while (*p == ' ') p++;
        }
    }

    task_t* proc = process_create(filename, node, argv_str, argv_len);
    if (!proc) {
        term_println(t, "run: falha ao criar processo (HEXEC invalido, sem memoria,");
        term_println(t, "     ou tabela de tasks cheia)");
        return;
    }
    proc->stdout_owner = t;
    *target_slot        = proc;
    *target_line_len    = 0;
    (void)target_line_buf; // já zerado via *target_line_len = 0; o
                             // conteúdo antigo do buffer é irrelevante
                             // até running_line_len/bg_line_len[i]
                             // voltar a crescer.

    if (background) {
        char msg[64];
        msg[0] = '\0';
        kstrncat(msg, "run: [", sizeof(msg));
        kstrncat(msg, filename, sizeof(msg));
        kstrncat(msg, "] iniciado em background", sizeof(msg));
        term_println(t, msg);
    } else {
        term_println(t, "run: processo iniciado");
    }
}

static void cmd_cat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_CAT_NEED_NAME)); return; }
    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), name, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, tr(STR_TERM_CAT_NOT_FOUND)); return; }
    if (node->type == VFS_DIR) { term_println(t, tr(STR_TERM_CAT_IS_DIR)); return; }
    if (!perm_allows(node, &cred, PERM_R)) { term_println(t, tr(STR_PERM_DENIED)); return; }
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

    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), fname, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) {
        int err = PERM_OK;
        node = vfs_touch_as(&cred, vfs_cwd(), fname, &err);
        if (!node && err == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    }
    if (!node || node->type != VFS_FILE) { term_println(t, tr(STR_TERM_WRITE_NOT_A_FILE)); return; }
    if (!perm_allows(node, &cred, PERM_W)) { term_println(t, tr(STR_PERM_DENIED)); return; }

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

    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), fname, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) {
        int err = PERM_OK;
        node = vfs_touch_as(&cred, vfs_cwd(), fname, &err);
        if (!node && err == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    }
    if (!node || node->type != VFS_FILE) { term_println(t, tr(STR_TERM_APPEND_NOT_A_FILE)); return; }
    if (!perm_allows(node, &cred, PERM_W)) { term_println(t, tr(STR_PERM_DENIED)); return; }

    vfs_append(node, rest);
    vfs_append(node, "\n");
    term_println(t, tr(STR_TERM_APPEND_DONE));
}

static void cmd_rm(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_RM_NEED_NAME)); return; }
    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), name, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, tr(STR_TERM_RM_NOT_FOUND)); return; }
    if (node == vfs_root()) { term_println(t, tr(STR_TERM_RM_CANT_ROOT)); return; }
    if (node == vfs_cwd()) { term_println(t, tr(STR_TERM_RM_CANT_CWD)); return; }
    // vfs_remove_as: W+X no diretório pai, regra sticky (/tmp) e, em
    // diretório, permissão para remover CADA item de dentro dele.
    int r = vfs_remove_as(&cred, node);
    if (r == PERM_EACCES)  term_println(t, tr(STR_PERM_DENIED));
    else if (r == PERM_OK) term_println(t, tr(STR_TERM_RM_DONE));
    else                   term_println(t, tr(STR_TERM_RM_NOT_FOUND));
}

static void cmd_stat(TermState* t, const char* name) {
    if (!name || !name[0]) { term_println(t, tr(STR_TERM_STAT_NEED_NAME)); return; }
    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), name, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, tr(STR_TERM_STAT_NOT_FOUND)); return; }
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
    {
        char pm[11]; perm_format(node, pm);
        char ow[USER_NAME_MAX]; owner_name(node->uid, ow, sizeof(ow));
        char oct[8]; // modo em octal (ex.: 0644)
        oct[0] = '0';
        oct[1] = (char)('0' + ((node->mode >> 9) & 7));
        oct[2] = (char)('0' + ((node->mode >> 6) & 7));
        oct[3] = (char)('0' + ((node->mode >> 3) & 7));
        oct[4] = (char)('0' + (node->mode & 7));
        oct[5] = '\0';
        kstrcpy(buf, tr(STR_TERM_STAT_OWNER)); kstrncat(buf, ow, sizeof(buf));
        term_println(t, buf);
        kstrcpy(buf, tr(STR_TERM_STAT_MODE)); kstrncat(buf, pm, sizeof(buf));
        kstrncat(buf, " (", sizeof(buf)); kstrncat(buf, oct, sizeof(buf)); kstrncat(buf, ")", sizeof(buf));
        term_println(t, buf);
    }
    char path[512];
    vfs_path_of(node, path, sizeof(path));
    kstrcpy(buf, tr(STR_TERM_STAT_PATH)); kstrncat(buf, path, sizeof(buf));
    term_println(t, buf);
}

// ============================================================
//  Usuários e permissões: whoami, id, users, adduser, deluser, passwd,
//  chmod, chown. Estes comandos agem em nome do usuário LOGADO
//  (session_*): administradores ignoram permissões de arquivo, mas os
//  programas que eles iniciam continuam sem privilégio (ver user.h).
// ============================================================

static const char* user_error_text(int r) {
    switch (r) {
        case USER_ERR_NOUSER:  return tr(STR_TERM_USER_NOT_FOUND);
        case USER_ERR_EXISTS:  return tr(STR_TERM_USER_EXISTS);
        case USER_ERR_INVALID: return tr(STR_TERM_USER_INVALID);
        case USER_ERR_LIMIT:   return tr(STR_TERM_USER_LIMIT);
        case USER_ERR_DENIED:  return tr(STR_TERM_USER_DENIED);
        case USER_ERR_IO:      return tr(STR_TERM_USER_IO);
        default:               return tr(STR_TERM_USER_IO);
    }
}

// Copia o primeiro token de `*args` (separado por espaço) para `out` e
// avança `*args` para depois dele. Retorna false se o token não coube em
// `cap` bytes (nome longo demais) — nesse caso `out` fica inválido.
static bool take_token(const char** args, char* out, size_t cap) {
    const char* p = *args;
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p && *p != ' ') {
        if (i + 1 >= cap) { *args = p; out[0] = '\0'; return false; }
        out[i++] = *p++;
    }
    out[i] = '\0';
    while (*p == ' ') p++;
    *args = p;
    return true;
}

static void cmd_whoami(TermState* t) {
    term_println(t, session_username());
}

static void cmd_id(TermState* t) {
    char line[TERM_COLS + 1];
    char num[16];
    kmemset(line, 0, sizeof(line));
    kstrcpy(line, "uid=");
    kuitoa(session_uid(), num);  kstrncat(line, num, sizeof(line));
    kstrncat(line, "(", sizeof(line));
    kstrncat(line, session_username(), sizeof(line));
    kstrncat(line, ") gid=", sizeof(line));
    kuitoa(session_gid(), num);  kstrncat(line, num, sizeof(line));
    if (session_is_admin()) kstrncat(line, tr(STR_TERM_ID_ADMIN), sizeof(line));
    term_println(t, line);
    term_println(t, tr(STR_TERM_ID_PROCS));
}

static void cmd_users(TermState* t) {
    term_println(t, tr(STR_TERM_USERS_HEADER));
    for (uint32_t i = 0; i < user_total(); i++) {
        const UserEntry* u = user_at(i);
        if (!u) continue;
        char line[TERM_COLS + 1];
        char num[16];
        kmemset(line, 0, sizeof(line));
        kuitoa(u->uid, num);
        append_padded(line, sizeof(line), num, 7);
        kstrncat(line, u->name, sizeof(line));
        if (user_is_admin(u)) kstrncat(line, tr(STR_TERM_ID_ADMIN), sizeof(line));
        term_println(t, line);
    }
}

static bool user_name_taken(const char* name) {
    for (uint32_t i = 0; i < user_total(); i++) {
        const UserEntry* u = user_at(i);
        if (!u) continue;
        const char* a = u->name;
        const char* b = name;
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*a && !*b) return true;
    }
    return false;
}

static void pw_reset(TermState* t) {
    kmemset(t->pw_first, 0, sizeof(t->pw_first));
    t->pw_stage = PW_NONE;
    t->pw_create = false;
    t->pw_admin = false;
    t->pw_target_uid = 0;
    t->pw_target_name[0] = '\0';
}

static const char* pw_prompt_text(const TermState* t) {
    switch (t->pw_stage) {
        case PW_CURRENT: return tr(STR_TERM_PW_CURRENT);
        case PW_NEW:     return tr(STR_TERM_PW_NEW);
        case PW_CONFIRM: return tr(STR_TERM_PW_CONFIRM);
        default:         return "";
    }
}

static void cmd_adduser(TermState* t, const char* args) {
    if (!session_is_admin()) { term_println(t, tr(STR_TERM_NEED_ADMIN)); return; }

    char name[USER_NAME_MAX];
    if (!take_token(&args, name, sizeof(name))) { term_println(t, tr(STR_TERM_USER_INVALID)); return; }
    if (!name[0]) { term_println(t, tr(STR_TERM_USAGE_ADDUSER)); return; }

    bool admin = false;
    if (*args) {
        if (kstrcmp(args, "admin") == 0) admin = true;
        else { term_println(t, tr(STR_TERM_USAGE_ADDUSER)); return; }
    }

    if (!user_name_valid(name)) { term_println(t, tr(STR_TERM_USER_INVALID)); return; }
    if (user_name_taken(name))  { term_println(t, tr(STR_TERM_USER_EXISTS)); return; }
    if (user_total() >= USER_MAX) { term_println(t, tr(STR_TERM_USER_LIMIT)); return; }

    pw_reset(t);
    t->pw_create = true;
    t->pw_admin  = admin;
    kstrncpy(t->pw_target_name, name, sizeof(t->pw_target_name) - 1);
    t->pw_stage = PW_NEW;
}

static void cmd_deluser(TermState* t, const char* args) {
    if (!session_is_admin()) { term_println(t, tr(STR_TERM_NEED_ADMIN)); return; }
    char name[USER_NAME_MAX];
    if (!take_token(&args, name, sizeof(name)) || !name[0]) {
        term_println(t, tr(STR_TERM_USAGE_DELUSER));
        return;
    }
    const UserEntry* u = user_find(name);
    if (!u) { term_println(t, tr(STR_TERM_USER_NOT_FOUND)); return; }
    int r = user_delete(u->uid);
    term_println(t, r == USER_OK ? tr(STR_TERM_USER_DELETED) : user_error_text(r));
}

static void cmd_passwd(TermState* t, const char* args) {
    char name[USER_NAME_MAX];
    if (!take_token(&args, name, sizeof(name))) { term_println(t, tr(STR_TERM_USER_INVALID)); return; }

    const UserEntry* u = name[0] ? user_find(name) : user_find_uid(session_uid());
    if (!u) { term_println(t, tr(STR_TERM_USER_NOT_FOUND)); return; }
    if (u->uid == UID_ROOT) { term_println(t, tr(STR_TERM_USER_DENIED)); return; }

    bool self = (u->uid == session_uid());
    if (!self && !session_is_admin()) { term_println(t, tr(STR_TERM_NEED_ADMIN)); return; }

    pw_reset(t);
    t->pw_target_uid = u->uid;
    kstrncpy(t->pw_target_name, u->name, sizeof(t->pw_target_name) - 1);
    // Trocar a PRÓPRIA senha pede a senha atual (se houver); um administrador
    // redefinindo a de OUTRA conta não precisa saber a antiga.
    t->pw_stage = (self && user_has_password(u)) ? PW_CURRENT : PW_NEW;
}

// Conclui adduser/passwd depois da confirmação.
static void pw_apply(TermState* t, const char* password) {
    int r;
    if (t->pw_create) {
        r = user_create(t->pw_target_name, password, t->pw_admin ? 1u : 0u);
        if (r == USER_OK) {
            const UserEntry* u = user_find(t->pw_target_name);
            char msg[TERM_COLS + 1];
            kstrncpy(msg, tr(STR_TERM_USER_CREATED), sizeof(msg) - 1);
            msg[sizeof(msg) - 1] = '\0';
            if (u) kstrncat(msg, u->home, sizeof(msg));
            term_println(t, msg);
        }
    } else {
        r = user_set_password(t->pw_target_uid, password);
        if (r == USER_OK) term_println(t, tr(STR_TERM_PW_CHANGED));
    }
    if (r != USER_OK) term_println(t, user_error_text(r));
}

// Chamada quando o usuário aperta Enter durante um prompt de senha.
// A linha digitada (t->input) NUNCA entra no histórico nem é mostrada.
static void term_password_line(TermState* t) {
    char buf[PASSWORD_MAX + 1];
    kstrncpy(buf, t->input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    term_println(t, pw_prompt_text(t));   // eco do prompt, sem a senha

    switch (t->pw_stage) {
        case PW_CURRENT:
            if (user_verify(t->pw_target_name, buf, nullptr) != USER_OK) {
                term_println(t, tr(STR_TERM_PW_WRONG));
                pw_reset(t);
            } else {
                t->pw_stage = PW_NEW;
            }
            break;
        case PW_NEW:
            kstrncpy(t->pw_first, buf, sizeof(t->pw_first) - 1);
            t->pw_first[sizeof(t->pw_first) - 1] = '\0';
            t->pw_stage = PW_CONFIRM;
            break;
        case PW_CONFIRM:
            if (kstrcmp(buf, t->pw_first) != 0) {
                term_println(t, tr(STR_TERM_PW_MISMATCH));
            } else {
                pw_apply(t, t->pw_first);
            }
            pw_reset(t);
            break;
        default:
            pw_reset(t);
            break;
    }
    kmemset(buf, 0, sizeof(buf));
}

// "644" / "0644" / "1777" -> 0644 ... (3 ou 4 dígitos octais)
static bool parse_octal_mode(const char* s, uint16_t* out) {
    size_t n = kstrlen(s);
    if (n < 3 || n > 4) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '7') return false;
        v = v * 8 + (uint32_t)(s[i] - '0');
    }
    if (v > VFS_MODE_MASK) return false;
    *out = (uint16_t)v;
    return true;
}

static void cmd_chmod(TermState* t, const char* args) {
    char mode_s[8];
    if (!take_token(&args, mode_s, sizeof(mode_s)) || !mode_s[0] || !*args) {
        term_println(t, tr(STR_TERM_USAGE_CHMOD));
        return;
    }
    uint16_t mode;
    if (!parse_octal_mode(mode_s, &mode)) { term_println(t, tr(STR_TERM_MODE_INVALID)); return; }

    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), args, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, tr(STR_TERM_STAT_NOT_FOUND)); return; }

    int r = vfs_chmod_as(&cred, node, mode);   // só o dono (ou administrador)
    term_println(t, r == PERM_OK ? tr(STR_TERM_DONE) : tr(STR_PERM_DENIED));
}

static void cmd_chown(TermState* t, const char* args) {
    if (!session_is_admin()) { term_println(t, tr(STR_TERM_NEED_ADMIN)); return; }
    char uname[USER_NAME_MAX];
    if (!take_token(&args, uname, sizeof(uname)) || !uname[0] || !*args) {
        term_println(t, tr(STR_TERM_USAGE_CHOWN));
        return;
    }
    const UserEntry* u = user_find(uname);
    if (!u) { term_println(t, tr(STR_TERM_USER_NOT_FOUND)); return; }

    Cred cred = session_cred();
    VfsNode* node = nullptr;
    int rr = vfs_resolve_as(&cred, vfs_cwd(), args, &node);
    if (rr == PERM_EACCES) { term_println(t, tr(STR_PERM_DENIED)); return; }
    if (rr != PERM_OK || !node) { term_println(t, tr(STR_TERM_STAT_NOT_FOUND)); return; }

    int r = vfs_chown_as(&cred, node, u->uid, u->gid);
    term_println(t, r == PERM_OK ? tr(STR_TERM_DONE) : tr(STR_PERM_DENIED));
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
    } else if (kstrcmp(cmd, "whoami") == 0) {
        cmd_whoami(t);
    } else if (kstrcmp(cmd, "id") == 0) {
        cmd_id(t);
    } else if (kstrcmp(cmd, "users") == 0) {
        cmd_users(t);
    } else if (kstrcmp(cmd, "adduser") == 0) {
        cmd_adduser(t, args);
    } else if (kstrcmp(cmd, "deluser") == 0) {
        cmd_deluser(t, args);
    } else if (kstrcmp(cmd, "passwd") == 0) {
        cmd_passwd(t, args);
    } else if (kstrcmp(cmd, "chmod") == 0) {
        cmd_chmod(t, args);
    } else if (kstrcmp(cmd, "chown") == 0) {
        cmd_chown(t, args);
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
        task_set_adopt_child_hook(term_adopt_child); // idempotente: sempre a mesma função
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
        // morto (Ctrl+C, enquanto em foco) ou colhido pelo reaper.
        if (m_state.running_process) {
            m_state.running_process->stdout_owner = nullptr;
            m_state.running_process = nullptr;
        }
        // Mesma desassociação para cada processo em background —
        // eles têm exatamente o mesmo risco de ponteiro pendurado,
        // só que em stdout_owner de várias tasks em vez de uma.
        for (int i = 0; i < TERM_MAX_BG_PROCESSES; i++) {
            if (m_state.bg_processes[i]) {
                m_state.bg_processes[i]->stdout_owner = nullptr;
                m_state.bg_processes[i] = nullptr;
            }
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
        term_update_bg_processes(t); // idem, mas para os processos em
                                       // background (ver comentário na
                                       // função) — independente de
                                       // running_process existir ou não.
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

        if (t->running_process) {
            // Processo em foco: a linha de input não mostra o prompt
            // do shell (haos:/caminho>) nem t->input — mostra o que
            // foi digitado ATÉ AGORA para o processo (running_line_buf),
            // que só vira uma linha de histórico de verdade quando um
            // '\n' fecha ela (ver on_event, ramo running_process,
            // acima). Sem isso, o usuário digitava "às cegas": o byte
            // já ia para o stdin_ring do processo, mas nada aparecia
            // na tela até apertar Enter.
            if (t->running_line_len > 0) {
                char echo_buf[TERM_COLS + 1];
                int copy = t->running_line_len < TERM_COLS ? t->running_line_len : TERM_COLS;
                kmemcpy(echo_buf, t->running_line_buf, (size_t)copy);
                echo_buf[copy] = '\0';
                fb_draw_string((uint32_t)bx, (uint32_t)input_y,
                               echo_buf, COLOR_TEXT_LIGHT, 0, true);
            }
            int cx = bx + FONT_W * t->running_line_len;
            uint32_t cur_color = t->cursor_visible ? COLOR_TERM_PROMPT : COLOR_TERM_BG;
            fb_fill_rect((uint32_t)cx, (uint32_t)(input_y + 1), 2, FONT_H - 3, cur_color);
            m_scrollbar.draw(0, 0);
            return;
        }

        char prompt[PROMPT_MAX + 1];
        char masked[PASSWORD_MAX + 2];
        const char* shown = t->input;
        if (t->pw_stage != PW_NONE) {
            // Prompt de senha: texto do prompt + um '*' por caractere digitado.
            kstrncpy(prompt, pw_prompt_text(t), PROMPT_MAX);
            prompt[PROMPT_MAX] = '\0';
            int n = t->input_len < PASSWORD_MAX ? t->input_len : PASSWORD_MAX;
            for (int i = 0; i < n; i++) masked[i] = '*';
            masked[n] = '\0';
            shown = masked;
        } else {
            term_build_prompt(prompt, sizeof(prompt));
        }
        int prompt_len = (int)kstrlen(prompt);

        fb_draw_string((uint32_t)bx, (uint32_t)input_y,
                       prompt, COLOR_TERM_PROMPT, 0, true);
        if (t->input_len > 0)
            fb_draw_string((uint32_t)(bx + FONT_W * prompt_len), (uint32_t)input_y,
                           shown, COLOR_TEXT_LIGHT, 0, true);

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

        // ---- Processo em foco: teclado vai para o stdin dele, não
        // para o prompt do shell ----
        //
        // Enquanto um processo estiver rodando "dentro" deste
        // terminal (t->running_process != nullptr), o usuário está
        // interagindo com AQUELE programa, não digitando um novo
        // comando de shell — então o comportamento normal de edição
        // de linha do prompt (histórico, backspace no buffer local,
        // Enter chamando term_execute) fica suspenso, e cada tecla
        // vira um byte no stdin_ring do processo via
        // task_stdin_write(). Só teclas de navegação/scroll (tratadas
        // acima, antes deste bloco) continuam funcionando — faz
        // sentido rolar a saída de um programa rodando.
        //
        // Ecoamos a tecla localmente (running_line_buf, o mesmo
        // buffer que term_update_running_process usa para acumular
        // saída do processo) para o usuário ver o que digitou — a
        // maioria dos programas de terminal em HAOS não implementa
        // eco próprio (não têm um "modo raw" configurável ainda),
        // então sem isso o usuário digitaria "às cegas".
        if (t->running_process) {
            task_t* proc = t->running_process;

            // ---- Ctrl+C: mata o processo em foco ----
            //
            // Convenção universal de terminal (Unix desde sempre,
            // Windows desde bem cedo também): Ctrl+C interrompe o
            // programa em execução, não fecha a janela nem é
            // repassado como um caractere normal para o stdin dele.
            // KEY_CTRL_C (ver kernel/keyboard.h) só chega aqui
            // quando ESTE widget tem foco de teclado, então é seguro
            // interpretá-lo como "matar processo" sem interferir com
            // o uso de Ctrl+C como "copiar" em outros lugares da GUI
            // (editor de texto, etc.) — cada widget decide o que
            // Ctrl+C significa para si.
            //
            // task_kill() (kernel/task.c) marca a task como ZOMBIE e
            // a remove do scheduler IMEDIATAMENTE — mesmo que ela
            // estivesse TASK_BLOCKED dormindo num sys_read(stdin)
            // esperando exatamente a tecla que acabou de chegar (ver
            // waiting_for_stdin em task.h). Depois disso o próximo
            // draw() (via term_update_running_process) vai detectar
            // has_exited=true e mostrar a mensagem final, exatamente
            // como para um processo que terminou sozinho.
            if (c == KEY_CTRL_C) {
                task_kill(proc);
                if (t->running_line_len > 0) {
                    t->running_line_buf[t->running_line_len] = '\0';
                    term_println(t, t->running_line_buf);
                    t->running_line_len = 0;
                }
                term_println(t, "^C");
                return EventResult::Handled;
            }

            if (c == '\n' || c == '\r') {
                uint8_t nl = '\n';
                task_stdin_write(proc, &nl, 1);
                // Fecha a linha ecoada visualmente, igual a uma linha
                // de saída normal do processo.
                t->running_line_buf[t->running_line_len] = '\0';
                term_println(t, t->running_line_buf);
                t->running_line_len = 0;
            } else if (c == '\b' || c == 0x08) {
                // Backspace: só tem efeito no eco visual local (apaga
                // o último caractere da linha ainda não enviada) —
                // NÃO enviamos um byte 0x08 para o stdin_ring, porque
                // a maioria dos programas simples aqui não interpreta
                // backspace como edição (eles leem byte a byte até um
                // '\n'); enviar 0x08 só apareceria como um caractere
                // de controle estranho no meio da entrada deles. Isso
                // é uma simplificação deliberada — um "modo raw" de
                // verdade, onde o próprio processo decide como tratar
                // backspace, fica para quando houver necessidade real
                // (ex.: um editor de linha interativo rodando como
                // processo de usuário).
                if (t->running_line_len > 0) t->running_line_len--;
            } else if (c >= 0x20 && c < 0x7F) {
                uint8_t byte = c;
                task_stdin_write(proc, &byte, 1);
                if (t->running_line_len < TERM_COLS) {
                    t->running_line_buf[t->running_line_len++] = (char)c;
                }
            }
            return EventResult::Handled;
        }

        // ---- Prompt de senha (passwd / adduser) ----
        // Tudo que for digitado vai para t->input, mas só aparece como '*'
        // (ver desenho acima) e nunca entra no histórico de comandos.
        if (t->pw_stage != PW_NONE) {
            if (c == '\n' || c == '\r') {
                term_password_line(t);
                kmemset(t->input, 0, TERM_BUF_SIZE);
                t->input_len = 0;
            } else if (c == '\b' || c == 0x08) {
                if (t->input_len > 0) t->input[--t->input_len] = 0;
            } else if (c == 27) {                       // ESC cancela
                term_println(t, tr(STR_CANCEL));
                pw_reset(t);
                kmemset(t->input, 0, TERM_BUF_SIZE);
                t->input_len = 0;
            } else if (c >= 0x20 && c < 0x7F) {
                if (t->input_len < PASSWORD_MAX) {
                    t->input[t->input_len++] = (char)c;
                    t->input[t->input_len] = 0;
                }
            }
            return EventResult::Handled;
        }

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
