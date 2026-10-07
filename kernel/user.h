#pragma once
#include "types.h"
#include "../fs/perm.h"

// ============================================================
//  kernel/user.h — Contas de usuário, senhas e sessão
//
//  Banco de usuários: /etc/passwd (texto, uma conta por linha):
//
//      nome:uid:gid:flags:home:salt:hash
//
//    flags : letras — 'A' administrador, 'L' conta travada (sem login) —
//            ou '-' se nenhuma
//    salt  : 32 hex (16 bytes aleatórios) — vazio = conta SEM senha
//    hash  : 64 hex = PBKDF2-HMAC-SHA256(senha, salt, 10000 iterações)
//
//  O arquivo é root:root com modo 0600, então nenhum processo de usuário
//  comum consegue nem LER os hashes (antes, /etc/user.cfg era legível por
//  qualquer programa).
//
//  Contas:
//    uid 0     "root"  — dono dos arquivos do sistema. Conta TRAVADA: ninguém
//                        entra como root; processos de usuário nunca rodam
//                        com uid 0.
//    uid 1000+ usuários. O primeiro criado (no primeiro boot) é
//                        administrador. Um administrador, no terminal/GUI,
//                        ignora permissões de arquivo (como root) e pode
//                        criar/remover contas, trocar senhas e dar arquivos.
//                        Os PROCESSOS que ele inicia continuam sem privilégio.
//
//  Migração: se existir o /etc/user.cfg da versão anterior, a conta é
//  convertida na primeira inicialização. O hash FNV-1a antigo é aceito UMA
//  vez e, no primeiro login bem-sucedido, substituído por PBKDF2 com salt.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define USER_NAME_MAX      32      // com o '\0'
#define USER_HOME_MAX      64
#define USER_MAX           16
#define PASSWORD_MAX       63      // caracteres
#define PBKDF2_ITERATIONS  10000

#define UID_ROOT           0
#define UID_FIRST_USER     1000

#define USER_FLAG_ADMIN    1u
#define USER_FLAG_LOCKED   2u

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    char     name[USER_NAME_MAX];
    char     home[USER_HOME_MAX];
    char     salt_hex[33];         // "" = sem senha; "fnv" = hash legado (migração)
    char     hash_hex[65];
} UserEntry;

enum {
    USER_OK          =  0,
    USER_ERR_NOUSER  = -1,
    USER_ERR_BADPW   = -2,
    USER_ERR_LOCKED  = -3,
    USER_ERR_EXISTS  = -4,
    USER_ERR_INVALID = -5,   // nome ou senha inválidos
    USER_ERR_LIMIT   = -6,   // USER_MAX contas
    USER_ERR_IO      = -7,
    USER_ERR_DENIED  = -8    // ex.: remover a si mesmo / o último administrador
};

// Carrega /etc/passwd (migrando o formato antigo, se preciso) e garante
// os diretórios de sistema (/etc /home /tmp /bin) com dono e modo corretos.
// Chamar depois de vfs_init() e antes do login.
void user_init(void);

// ---- Consulta ----
uint32_t         user_total(void);                  // inclui root
const UserEntry* user_at(uint32_t index);           // 0..user_total()-1
uint32_t         user_regular_count(void);          // contas que podem entrar (sem root/travadas)
const UserEntry* user_find(const char* name);
const UserEntry* user_find_uid(uint32_t uid);
bool             user_has_password(const UserEntry* u);
bool             user_is_admin(const UserEntry* u);

// Nome: 1..31 bytes, sem ':' '/' '\\', sem controles nem espaços, e não pode
// ser "." / "..". user_sanitize_name troca os inválidos por '_' (e apara).
bool user_name_valid(const char* name);
void user_sanitize_name(char* name);

// ---- Autenticação ----
// Confere nome+senha. Em sucesso devolve USER_OK e (se `out` != NULL) a conta.
// Conta sem senha só aceita senha vazia.
int  user_verify(const char* name, const char* password, const UserEntry** out);

// ---- Administração (quem chama decide se o usuário pode; ver terminal) ----
// Cria a conta + /home/<nome> (modo 0700, dono = nova conta). password pode
// ser NULL/"" (sem senha). Tudo numa única gravação em disco.
// `admin` é uint32_t (não bool) de propósito: este header é usado por C e C++,
// e `bool` tem representações diferentes nas duas linguagens neste projeto.
int  user_create(const char* name, const char* password, uint32_t admin);
int  user_set_password(uint32_t uid, const char* password);   // ""/NULL = remove a senha
int  user_delete(uint32_t uid);   // a home fica no disco, passada para root

// ---- Sessão (quem está logado na GUI) ----
void        session_start(const UserEntry* u);   // define o usuário logado + cwd = home
bool        session_active(void);
uint32_t    session_uid(void);
uint32_t    session_gid(void);
bool        session_is_admin(void);
const char* session_username(void);              // "" se ninguém logado
const char* session_home(void);                  // "/" se ninguém logado

// Credenciais do usuário logado para código de KERNEL agindo em nome dele
// (terminal, editor, explorador). Administrador => bypass.
Cred        session_cred(void);

// Credenciais para um PROCESSO (syscalls): uid/gid da task, bypass só uid 0.
Cred        cred_for_ids(uint32_t uid, uint32_t gid);

#ifdef __cplusplus
}
#endif
