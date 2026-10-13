#pragma once
#include "types.h"

// ============================================================
//  syscall.h — Syscalls do HAOS via SYSCALL/SYSRET
//
//  Convenção de chamada (System V AMD64, adaptada para syscall):
//    rax = número da syscall
//    rdi, rsi, rdx, r10, r8, r9 = argumentos (nessa ordem)
//      (usa r10 no lugar de rcx porque SYSCALL destrói rcx/r11
//       automaticamente — a ABI padrão do Linux x86_64 faz o mesmo
//       ajuste pelo mesmo motivo)
//    retorno em rax
//
//  Um processo de usuário simplesmente executa a instrução
//  `syscall` — não existe int 0x80 aqui, usamos o mecanismo mais
//  moderno e mais rápido (SYSCALL/SYSRET, sem passar pela IDT).
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// ---- Números de syscall ----
#define SYS_WRITE     0  // sys_write(const char* str, uint64_t len) -> bytes escritos
                          // (sempre vai para o stdout do processo —
                          // ver task_stdout_write. Para escrever em
                          // ARQUIVO, use SYS_WRITE_FD.)
#define SYS_EXIT      1  // sys_exit(int code) -> nunca retorna
#define SYS_SBRK      2  // sys_sbrk(int64_t increment) -> endereço do topo
                          // ANTERIOR da heap (convenção clássica de
                          // sbrk() Unix), ou (uint64_t)-1 em erro. Base
                          // de malloc()/free() na libc.
#define SYS_OPEN      3  // sys_open(const char* path, uint64_t path_len,
                          //          uint64_t flags)
                          // -> descritor de arquivo (>= 0) ou -1 em erro.
                          // flags é uma bitmask — ver O_* abaixo. Sem
                          // O_CREAT, o arquivo deve existir; com
                          // O_CREAT, é criado se ainda não existir
                          // (mas NÃO é erro se já existir — igual ao
                          // open() POSIX sem O_EXCL). Terceiro
                          // argumento é NOVO nesta versão — qualquer
                          // chamador antigo que só passava
                          // (path, path_len) precisa ser atualizado
                          // para passar flags=O_RDONLY (0) também.
#define SYS_READ      4  // sys_read(int fd, void* buf, uint64_t count)
                          // -> bytes lidos (0 = fim de arquivo) ou -1
                          // em erro (fd inválido, ponteiro inválido).
                          // fd == 0 é especial: stdin (teclado), lido
                          // do terminal dono deste processo. Ao
                          // contrário de um fd de arquivo, BLOQUEIA
                          // (não retorna) até haver pelo menos 1 byte
                          // disponível — ver sys_read_stdin em
                          // syscall.c. Retorna 0 (EOF) só se o terminal
                          // dono não existir mais (janela fechada).
#define SYS_CLOSE     5  // sys_close(int fd) -> 0 em sucesso, -1 em erro.
#define SYS_WRITE_FD  6  // sys_write_fd(int fd, const void* buf, uint64_t count)
                          // -> bytes escritos ou -1 em erro (fd
                          // inválido, fd não aberto com O_WRONLY,
                          // ponteiro inválido, ou arquivo atingiu
                          // VFS_FILE_MAX). Escreve na posição atual
                          // do fd (fd_offset), crescendo o arquivo
                          // (vfs_ensure_capacity) se necessário —
                          // NÃO trunca o que já havia além do ponto
                          // de escrita, igual a um write() POSIX
                          // normal (só O_TRUNC no open trunca, ver
                          // sys_open). fd == 0/1/2 não são válidos
                          // aqui (não há stdout/stdin "de arquivo").
#define SYS_SPAWN     7  // sys_spawn(const char* path, uint64_t path_len,
                          //           const char* argv_str, uint64_t argv_len)
                          // -> id do processo filho (>= 1) em sucesso,
                          // (uint64_t)-1 em erro. Esse id é o que
                          // SYS_WAITPID espera receber depois.
                          // Cria um NOVO processo a partir do executável
                          // HEXEC em `path` (caminho absoluto no VFS) —
                          // equivalente ao CreateProcess() do Windows /
                          // posix_spawn() do POSIX. argv_str segue o
                          // mesmo formato do lançamento pelo terminal:
                          // tokens terminados em '\0', com argv[0]
                          // (o nome do programa) como PRIMEIRO token;
                          // se argv_len == 0, o kernel usa só o
                          // próprio caminho como argv[0].
                          // O filho roda em background do MESMO
                          // terminal do pai (a saída dele aparece lá,
                          // prefixada com o nome) e NÃO herda stdin.
                          // Só funciona se o pai tem um terminal dono
                          // (stdout_owner) — senão retorna -1.
                          // POR QUE não é fork(): fork() clona o
                          // contexto de execução do pai (registradores
                          // + pilha de kernel no ponto exato da
                          // syscall) e retorna duas vezes. Isso exige
                          // mexer no stub de syscall e no scheduler de
                          // um jeito que não dá para validar sem bootar
                          // o SO; spawn cobre o caso de uso real
                          // ("um programa inicia outro") reaproveitando
                          // process_create, que já é exercitado por
                          // "run" no terminal.
#define SYS_WAITPID   8  // sys_waitpid(uint64_t pid, uint64_t* out_exit_code)
                          // -> 1 em sucesso (*out_exit_code preenchido,
                          // se o ponteiro for não-NULO e válido), 0 se
                          // `pid` não corresponde a nenhum processo
                          // (nunca existiu ou já foi reciclado), -1 em
                          // erro (pid == 0, ou outro processo já está
                          // esperando por ele — só um waiter por vez).
                          // BLOQUEIA (não retorna) até o processo `pid`
                          // chamar sys_exit — mesmo mecanismo de
                          // TASK_BLOCKED usado por SYS_READ em fd=0.
                          // `pid` é o valor devolvido por SYS_SPAWN.
                          // Não há checagem de parentesco: qualquer
                          // processo que conheça o id pode esperar por
                          // ele, não só quem o criou.
#define SYS_GETPID    9  // sys_getpid() -> id do processo atual (o MESMO valor que
                          // SYS_SPAWN devolve ao pai e que SYS_WAITPID espera).
#define SYS_YIELD     10 // sys_yield() -> 0. Cede a CPU voluntariamente.
#define SYS_UPTIME_MS 11 // sys_uptime_ms() -> milissegundos desde o boot
                          // (resolução de 10 ms: o PIT roda a 100 Hz).
#define SYS_SLEEP_MS  12 // sys_sleep_ms(uint64_t ms) -> 0. Dorme pelo menos `ms`
                          // milissegundos (arredondado para cima ao tick de
                          // 10 ms; máx. SYS_SLEEP_MAX_MS, valores maiores são
                          // limitados). Implementado cedendo a CPU em laço até o
                          // prazo — a task continua escalonável, só não faz nada.
#define SYS_STAT      13 // sys_stat(const char* path, uint64_t path_len,
                          //          haos_stat_t* out) -> 0 ou -1 (não existe /
                          // ponteiro inválido). Caminho relativo é resolvido a
                          // partir do cwd do processo.
#define SYS_READDIR   14 // sys_readdir(const char* path, uint64_t path_len,
                          //             uint64_t index, haos_dirent_t* out)
                          // -> 1 (out preenchido), 0 (index além da última
                          // entrada = fim) ou -1 (erro / não é diretório).
                          // Sem estado no kernel: o chamador itera index=0,1,2...
#define SYS_GETCWD    15 // sys_getcwd(char* buf, uint64_t size) -> tamanho do caminho
                          // (sem o '\0'; o '\0' também é escrito) ou -1 se `size`
                          // for pequeno demais / ponteiro inválido.
#define SYS_CHDIR     16 // sys_chdir(const char* path, uint64_t path_len) -> 0 ou -1.
                          // Só afeta ESTE processo (cwd por processo).
#define SYS_MKDIR     17 // sys_mkdir(const char* path, uint64_t path_len) -> 0 ou -1
                          // (já existe, pai inexistente, nome longo, VFS cheio...).
                          // Não cria diretórios intermediários.
#define SYS_UNLINK    18 // sys_unlink(const char* path, uint64_t path_len) -> 0 ou -1.
                          // Remove arquivo OU diretório VAZIO. Diretório com
                          // conteúdo, a raiz e o cwd do terminal são recusados.
                          // Descritores abertos para o arquivo passam a falhar
                          // (-1) em read/write — ver VfsNode::removed.
#define SYS_LSEEK     19 // sys_lseek(int fd, int64_t offset, int whence) -> nova
                          // posição (>= 0) ou -1. Só para fds de arquivo (>= 3).
                          // Posição fica limitada a [0, VFS_FILE_MAX].
// ---- Janelas gráficas (Ring 3) ----
// Um processo cria janelas com um canvas de pixels próprio; desenha
// nele por syscalls e recebe teclado/mouse como eventos. Detalhes e
// as structs de troca: gui/core/userwin.h (espelhadas em
// libc/include/haos/gfx.h). `handle` é um inteiro pequeno (>= 0) que só
// vale para o processo que criou a janela. Todas devolvem -1 se o
// handle é inválido OU a janela já foi fechada (pelo usuário, no X) —
// é assim que o programa descobre que deve encerrar.
#define SYS_WIN_CREATE  20 // sys_win_create(w, h, title, title_len) -> handle ou -1.
                            // w,h = tamanho da ÁREA CLIENTE (o kernel ajusta a
                            // limites razoáveis; use SYS_WIN_INFO p/ saber o real).
                            // No máx. 4 janelas por processo.
#define SYS_WIN_DESTROY 21 // sys_win_destroy(handle) -> 0 ou -1.
#define SYS_WIN_FILL    22 // sys_win_fill(handle, haos_win_rect_t*) -> 0 ou -1.
#define SYS_WIN_TEXT    23 // sys_win_text(handle, haos_win_text_t*) -> 0 ou -1.
#define SYS_WIN_BLIT    24 // sys_win_blit(handle, haos_win_blit_t*) -> 0 ou -1.
#define SYS_WIN_EVENT   25 // sys_win_event(handle, haos_win_event_t* out, timeout_ms)
                            // -> 1 (evento), 0 (nenhum/timeout) ou -1 (janela
                            // inexistente/fechada). timeout_ms = 0 só consulta;
                            // 0xFFFFFFFF espera para sempre.
#define SYS_WIN_INFO    26 // sys_win_info(handle, haos_win_info_t* out) -> 0 ou -1.
#define SYS_GETUID      27 // sys_getuid() -> uid do processo (nunca 0 em processos de usuário).
#define SYS_GETGID      28 // sys_getgid() -> gid do processo.
#define SYS_CHMOD       29 // sys_chmod(const char* path, uint64_t path_len, uint64_t mode)
                            // -> 0 ou -1. Só o DONO do arquivo muda as permissões;
                            // mode = 0..01777 (rwxrwxrwx + sticky).
#define SYS_GETRANDOM 30 // sys_getrandom(void* buf, uint64_t len) -> len ou -1. len <= 256.
#define SYS_RENAME    31 // sys_rename(old, old_len, new, new_len) -> 0 ou -1. Move/renomeia; não sobrescreve.
#define SYS_SET_WALLPAPER 32 // sys_set_wallpaper(pixels, w, h) -> 0 ou -1. 0x00RRGGBB, w<=1024, h<=768. Global.
#define SYS_CLIPBOARD 33 // sys_clipboard(op, buf, len): op 0 = GET (copia até len bytes p/ buf, devolve o
                         //   tamanho TOTAL guardado); op 1 = SET (len <= 64 KiB; 0 limpa) -> 0 ou -1.
#define SYS_FONT_DATA 34 // sys_font_data(buf, cap): 4096 bytes = 256 glifos 8x16 (CP437), 1 bit/pixel,
                         //   MSB à esquerda -> devolve 4096 ou -1.
#define SYS_THEME_GET 35 // sys_theme_get(buf, count): uint32_t[count] (ver libc/include/haos/uitheme.h);
                         //   devolve o nº de entradas existentes ou -1. [0] = "geração" (muda com o tema).
#define SYS_WIN_SET_TITLE 36 // sys_win_set_title(handle, title, len) -> 0 ou -1.
#define SYS_COUNT     37

// ---- whence de sys_lseek ----
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

// ---- Limite de SYS_SLEEP_MS (1 hora) ----
#define SYS_SLEEP_MAX_MS  (60ULL * 60ULL * 1000ULL)

// ---- Estruturas trocadas com o espaço de usuário ----
// ESPELHADAS em libc/include/haos/fs.h — mantenha o layout idêntico.
#define HAOS_TYPE_DIR   0   // == VFS_DIR
#define HAOS_TYPE_FILE  1   // == VFS_FILE
#define HAOS_NAME_MAX   64  // == VFS_NAME_MAX (inclui o '\0')

typedef struct {
    uint32_t type;   // HAOS_TYPE_DIR ou HAOS_TYPE_FILE
    uint32_t size;   // arquivo: bytes; diretório: nº de entradas
} haos_stat_t;

typedef struct {
    uint32_t type;
    uint32_t size;               // idem haos_stat_t::size
    char     name[HAOS_NAME_MAX];// terminado em '\0'
} haos_dirent_t;

// ---- Flags de sys_open (bitmask) ----
#define O_RDONLY  0x0   // abre para leitura (comportamento padrão,
                          // idêntico ao de antes desta flag existir)
#define O_WRONLY  0x1   // abre para ESCRITA via SYS_WRITE_FD — sem
                          // esta flag, SYS_WRITE_FD nesse fd falha
                          // (-1), mesmo que o arquivo exista e seja
                          // gravável em tese — a intenção declarada
                          // no open() é o que decide, igual ao POSIX.
#define O_CREAT   0x2   // cria o arquivo se não existir (dentro de um
                          // diretório que já existe — sys_open não
                          // cria diretórios intermediários). Sem
                          // efeito se o arquivo já existir.
#define O_TRUNC   0x4   // se o arquivo já existir, esvazia o conteúdo
                          // dele (size=0) no momento do open — só faz
                          // sentido combinado com O_WRONLY.

// Configura os MSRs necessários (STAR, LSTAR, SFMASK) e habilita
// EFER.SCE (System Call Extensions). Deve ser chamado depois de
// gdt_init() (precisa dos seletores de kernel/usuário já definidos)
// e antes de qualquer enter_usermode().
void syscall_init(void);

// Dispatcher chamado pelo stub em assembly (syscall_asm.asm). Não
// chame diretamente — é o ponto de entrada em C de toda syscall.
// Suporta até 5 argumentos (a1..a5), suficiente para sys_write
// (ptr, len) e sys_exit (code); pode ser estendido para 6 se algum
// dia for necessário (ver comentário em syscall_asm.asm).
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5);

#ifdef __cplusplus
}
#endif
