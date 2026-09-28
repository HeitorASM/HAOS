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
                          // -> 1 em sucesso, -1 em erro.
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
#define SYS_COUNT     8

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
