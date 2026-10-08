#include "user.h"
#include "sha256.h"
#include "random.h"
#include "memory.h"
#include "lang.h"
#include "../fs/vfs.h"
#include "../drivers/rtc.h"

// timer_ticks é incrementado a 100 Hz pelo IRQ0 (idt_asm.asm); aqui só serve
// como uma das fontes de variação do salt.
extern volatile uint64_t timer_ticks;

// ---- Estado -----------------------------------------------------------
//
// NOTA: cópias de UserEntry sempre com kmemcpy — o kernel não define um
// memcpy "de verdade", e uma atribuição de struct (`a = b`) deste tamanho
// pode virar uma chamada implícita a memcpy, que não existe no link.

static UserEntry g_users[USER_MAX];
static uint32_t  g_user_count = 0;

static struct {
    bool      active;
    UserEntry user;
} g_session;

// ---- Helpers de string/hex -------------------------------------------

static void copy_str(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    if (cap == 0) return;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Prefixo exato (não depende de kstrncmp).
static bool starts_with(const char* s, size_t avail, const char* prefix) {
    size_t n = kstrlen(prefix);
    if (avail < n) return false;
    for (size_t i = 0; i < n; i++) if (s[i] != prefix[i]) return false;
    return true;
}

static char lower_c(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool eq_nocase(const char* a, const char* b) {
    while (*a && *b) {
        if (lower_c(*a) != lower_c(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static void hex_encode(const uint8_t* in, size_t n, char* out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0xF];
    }
    out[n * 2] = '\0';
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodifica EXATAMENTE 2*n dígitos hex.
static bool hex_decode(const char* in, uint8_t* out, size_t n) {
    if (kstrlen(in) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(in[i * 2]);
        int lo = hex_val(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool is_hex_string(const char* s, size_t n) {
    if (kstrlen(s) != n) return false;
    for (size_t i = 0; i < n; i++) if (hex_val(s[i]) < 0) return false;
    return true;
}

static bool parse_u32(const char* s, uint32_t* out) {
    if (!s || !*s) return false;
    uint64_t v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFULL) return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void append(char* buf, size_t cap, size_t* len, const char* s) {
    while (*s && *len + 1 < cap) buf[(*len)++] = *s++;
    buf[*len] = '\0';
}

static void append_u32(char* buf, size_t cap, size_t* len, uint32_t v) {
    char tmp[24];
    kuitoa(v, tmp);
    append(buf, cap, len, tmp);
}

// ---- Hash legado (só para migrar /etc/user.cfg da versão anterior) ------

static uint32_t fnv1a_hash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void fnv_hex(uint32_t h, char out[9]) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) { out[i] = digits[h & 0xF]; h >>= 4; }
    out[8] = '\0';
}

// ---- Aleatoriedade (só para o SALT) ---------------------------------
//
// Não há gerador de números aleatórios de hardware. O salt NÃO precisa ser
// secreto — só diferente para cada senha (derrota tabelas pré-computadas e
// faz duas contas com a mesma senha terem hashes diferentes). Misturamos
// fontes que variam entre boots e entre chamadas (TSC, ticks do PIT, relógio
// RTC, endereço de pilha, contador) com SHA-256, encadeando o estado.

static void random_bytes(uint8_t* out, size_t n) {
    rng_bytes(out, n);   // kernel/random.c: RDSEED/RDRAND + jitter + DRBG
}

// ---- Senhas -------------------------------------------------------------

static void hash_password(const char* password, const uint8_t salt[16], char hash_hex[65]) {
    uint8_t dk[SHA256_DIGEST_SIZE];
    pbkdf2_hmac_sha256((const uint8_t*)password, kstrlen(password),
                       salt, 16, PBKDF2_ITERATIONS, dk);
    hex_encode(dk, SHA256_DIGEST_SIZE, hash_hex);
    kmemset(dk, 0, sizeof(dk));
}

// Define (ou remove, se vazia) a senha de `u` com um salt novo.
static void entry_set_password(UserEntry* u, const char* password) {
    if (!password || !password[0]) {
        u->salt_hex[0] = '\0';
        u->hash_hex[0] = '\0';
        return;
    }
    uint8_t salt[16];
    random_bytes(salt, sizeof(salt));
    hex_encode(salt, sizeof(salt), u->salt_hex);
    hash_password(password, salt, u->hash_hex);
}

static int entry_check_password(const UserEntry* u, const char* password,
                                bool* needs_upgrade) {
    *needs_upgrade = false;
    if (!password) password = "";

    if (!u->salt_hex[0])                       // conta sem senha
        return password[0] == '\0' ? USER_OK : USER_ERR_BADPW;

    if (kstrcmp(u->salt_hex, "fnv") == 0) {    // hash legado: aceita e migra
        char hx[9];
        fnv_hex(fnv1a_hash(password), hx);
        if (kstrlen(u->hash_hex) == 8 && ct_equal(hx, u->hash_hex, 8)) {
            *needs_upgrade = true;
            return USER_OK;
        }
        return USER_ERR_BADPW;
    }

    uint8_t salt[16];
    if (!hex_decode(u->salt_hex, salt, sizeof(salt))) return USER_ERR_BADPW;
    char hx[65];
    hash_password(password, salt, hx);
    int ok = kstrlen(u->hash_hex) == 64 && ct_equal(hx, u->hash_hex, 64);
    kmemset(hx, 0, sizeof(hx));
    return ok ? USER_OK : USER_ERR_BADPW;
}

// ---- Nomes -------------------------------------------------------------

static bool name_char_ok(unsigned char c) {
    if (c <= 0x20 || c == 0x7F) return false;        // controles e espaço
    if (c == ':' || c == '/' || c == '\\') return false;
    return true;                                      // inclui UTF-8 (>= 0x80)
}

bool user_name_valid(const char* name) {
    if (!name) return false;
    size_t len = kstrlen(name);
    if (len == 0 || len >= USER_NAME_MAX) return false;
    if (name[0] == '-') return false;                // confundiria com opção de comando
    if (kstrcmp(name, ".") == 0 || kstrcmp(name, "..") == 0) return false;
    for (size_t i = 0; i < len; i++)
        if (!name_char_ok((unsigned char)name[i])) return false;
    return true;
}

void user_sanitize_name(char* name) {
    if (!name) return;
    size_t len = kstrlen(name);
    size_t start = 0;
    while (start < len && name[start] == ' ') start++;
    while (len > start && name[len - 1] == ' ') len--;

    size_t out = 0;
    for (size_t i = start; i < len && out < USER_NAME_MAX - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        name[out++] = name_char_ok(c) ? (char)c : '_';
    }
    name[out] = '\0';

    if (out > 0 && name[0] == '-') name[0] = '_';
    if (kstrcmp(name, ".") == 0 || kstrcmp(name, "..") == 0) {
        name[0] = '_';
        name[1] = '\0';
    }
}

// ---- /etc/passwd: leitura e gravação ---------------------------------------

static bool entry_valid_fields(const UserEntry* u) {
    if (!user_name_valid(u->name)) return false;
    if (u->home[0] != '/' || kstrlen(u->home) >= USER_HOME_MAX) return false;
    if (u->salt_hex[0] == '\0')
        return u->hash_hex[0] == '\0';
    if (kstrcmp(u->salt_hex, "fnv") == 0)
        return is_hex_string(u->hash_hex, 8);
    return is_hex_string(u->salt_hex, 32) && is_hex_string(u->hash_hex, 64);
}

static const UserEntry* find_nocase(const char* name) {
    for (uint32_t i = 0; i < g_user_count; i++)
        if (eq_nocase(g_users[i].name, name)) return &g_users[i];
    return NULL;
}

// "nome:uid:gid:flags:home:salt:hash" — modifica `line` (troca ':' por '\0').
static bool parse_line(char* line, UserEntry* u) {
    char* f[7];
    int n = 0;
    f[n++] = line;
    for (char* p = line; *p && n < 7; p++) {
        if (*p == ':') { *p = '\0'; f[n++] = p + 1; }
    }
    if (n < 7) return false;

    kmemset(u, 0, sizeof(*u));
    copy_str(u->name, sizeof(u->name), f[0]);
    if (!parse_u32(f[1], &u->uid)) return false;
    if (!parse_u32(f[2], &u->gid)) return false;
    for (const char* p = f[3]; *p; p++) {
        if (*p == 'A') u->flags |= USER_FLAG_ADMIN;
        else if (*p == 'L') u->flags |= USER_FLAG_LOCKED;
        else if (*p != '-') return false;
    }
    copy_str(u->home,     sizeof(u->home),     f[4]);
    copy_str(u->salt_hex, sizeof(u->salt_hex), f[5]);
    copy_str(u->hash_hex, sizeof(u->hash_hex), f[6]);
    return entry_valid_fields(u);
}

static void load_passwd(const VfsNode* f) {
    char* copy = (char*)kmalloc((size_t)f->size + 1);
    if (!copy) return;
    if (f->size > 0 && f->data) kmemcpy(copy, f->data, f->size);
    copy[f->size] = '\0';

    char* p = copy;
    while (*p) {
        char* line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = '\0'; p++; }
        if (line[0] == '\0' || line[0] == '#') continue;

        UserEntry e;
        kmemset(&e, 0, sizeof(e));
        if (!parse_line(line, &e)) continue;              // linha corrompida: ignora
        if (g_user_count >= USER_MAX) break;
        if (user_find(e.name) || user_find_uid(e.uid)) continue;
        kmemcpy(&g_users[g_user_count++], &e, sizeof(e));
    }
    kmemset(copy, 0, (size_t)f->size + 1);
    kfree(copy);
}

static VfsNode* ensure_dir(VfsNode* parent, const char* name, uint16_t mode, bool force_mode) {
    VfsNode* d = vfs_find_child(parent, name);
    if (d && d->type != VFS_DIR) return NULL;
    bool created = false;
    if (!d) { d = vfs_mkdir(parent, name); created = true; }
    if (d && (created || force_mode)) vfs_set_owner(d, 0, 0, mode);
    return d;
}

// Grava /etc/passwd (root:root 0600). Num lote: o arquivo nunca chega ao
// disco com o dono/modo padrão (0644) nem por um instante.
static bool save_passwd(void) {
    VfsNode* etc = ensure_dir(vfs_root(), "etc", VFS_MODE_DIR_DEFAULT, false);
    if (!etc) return false;

    vfs_batch_begin();
    VfsNode* f = vfs_find_child(etc, "passwd");
    if (!f) f = vfs_touch(etc, "passwd");
    bool ok = f && f->type == VFS_FILE;

    if (ok) {
        size_t cap = (size_t)(g_user_count ? g_user_count : 1) * 256 + 16;
        char* buf = (char*)kzalloc(cap);
        if (!buf) {
            ok = false;
        } else {
            size_t len = 0;
            for (uint32_t i = 0; i < g_user_count; i++) {
                const UserEntry* u = &g_users[i];
                append(buf, cap, &len, u->name);          append(buf, cap, &len, ":");
                append_u32(buf, cap, &len, u->uid);       append(buf, cap, &len, ":");
                append_u32(buf, cap, &len, u->gid);       append(buf, cap, &len, ":");
                if (u->flags & USER_FLAG_ADMIN)  append(buf, cap, &len, "A");
                if (u->flags & USER_FLAG_LOCKED) append(buf, cap, &len, "L");
                if (!(u->flags & (USER_FLAG_ADMIN | USER_FLAG_LOCKED)))
                    append(buf, cap, &len, "-");
                append(buf, cap, &len, ":");
                append(buf, cap, &len, u->home);          append(buf, cap, &len, ":");
                append(buf, cap, &len, u->salt_hex);      append(buf, cap, &len, ":");
                append(buf, cap, &len, u->hash_hex);      append(buf, cap, &len, "\n");
            }
            ok = vfs_write(f, buf);
            kmemset(buf, 0, cap);
            kfree(buf);
        }
        vfs_set_owner(f, 0, 0, 0600);
    }
    bool flushed = vfs_batch_end();
    return ok && flushed;
}

// ---- Consulta ---------------------------------------------------------------

uint32_t user_total(void) { return g_user_count; }

const UserEntry* user_at(uint32_t index) {
    return index < g_user_count ? &g_users[index] : NULL;
}

uint32_t user_regular_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid != UID_ROOT && !(g_users[i].flags & USER_FLAG_LOCKED)) n++;
    return n;
}

const UserEntry* user_find(const char* name) {
    if (!name) return NULL;
    return find_nocase(name);     // mesma regra de user_create: Ana == ana
}

const UserEntry* user_find_uid(uint32_t uid) {
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid == uid) return &g_users[i];
    return NULL;
}

bool user_has_password(const UserEntry* u) { return u && u->salt_hex[0] != '\0'; }
bool user_is_admin(const UserEntry* u)     { return u && (u->flags & USER_FLAG_ADMIN); }

// ---- Inicialização / migração ----------------------------------------------

static void add_root_entry(void) {
    if (user_find_uid(UID_ROOT) || g_user_count >= USER_MAX) return;
    UserEntry r;
    kmemset(&r, 0, sizeof(r));
    r.uid = UID_ROOT;
    r.gid = UID_ROOT;
    r.flags = USER_FLAG_LOCKED;
    copy_str(r.name, sizeof(r.name), "root");
    copy_str(r.home, sizeof(r.home), "/");
    kmemcpy(&g_users[g_user_count++], &r, sizeof(r));
}

// Dono/modo dos diretórios de sistema. Roda quando o sistema ainda não tem
// /etc/passwd (primeiro boot ou disco da versão sem usuários): nesse caso os
// diretórios vieram com o padrão de migração (root:root 0755) e /tmp precisa
// virar gravável por todos. Num sistema já configurado NÃO mexemos — o
// administrador pode ter ajustado esses modos de propósito.
static void fix_system_dirs(void) {
    VfsNode* root = vfs_root();
    vfs_set_owner(root, 0, 0, VFS_MODE_DIR_DEFAULT);
    ensure_dir(root, "etc",  VFS_MODE_DIR_DEFAULT, true);
    ensure_dir(root, "home", VFS_MODE_DIR_DEFAULT, true);
    ensure_dir(root, "bin",  VFS_MODE_DIR_DEFAULT, true);
    ensure_dir(root, "tmp",  0777 | VFS_MODE_STICKY, true);
}

// Converte o /etc/user.cfg ("name=..." / "pwhash=...") da versão anterior.
// Retorna true se havia uma conta para migrar.
static bool migrate_user_cfg(VfsNode* etc) {
    VfsNode* cfg = vfs_find_child(etc, "user.cfg");
    if (!cfg || cfg->type != VFS_FILE || !cfg->data || cfg->size == 0) return false;

    char name[USER_NAME_MAX] = {0};
    char pwhash[16] = {0};
    const char* d = cfg->data;
    uint32_t pos = 0;
    while (pos < cfg->size) {
        const char* key = NULL;
        char* dst = NULL;
        size_t cap = 0, klen = 0;
        if (starts_with(d + pos, cfg->size - pos, "name=")) {
            key = "name="; dst = name; cap = sizeof(name); klen = 5;
        } else if (starts_with(d + pos, cfg->size - pos, "pwhash=")) {
            key = "pwhash="; dst = pwhash; cap = sizeof(pwhash); klen = 7;
        }
        if (key) {
            uint32_t j = 0;
            uint32_t p = pos + (uint32_t)klen;
            while (p < cfg->size && d[p] != '\n' && d[p] != '\0' && j < cap - 1) dst[j++] = d[p++];
            dst[j] = '\0';
        }
        while (pos < cfg->size && d[pos] != '\n') pos++;
        if (pos < cfg->size) pos++;
    }

    user_sanitize_name(name);
    if (name[0] == '\0') return false;

    UserEntry u;
    kmemset(&u, 0, sizeof(u));
    u.uid = UID_FIRST_USER;
    u.gid = UID_FIRST_USER;
    u.flags = USER_FLAG_ADMIN;                    // era o único usuário: vira administrador
    copy_str(u.name, sizeof(u.name), name);

    // A versão anterior usava /home/user como home de todo mundo. Mantemos
    // essa pasta (com os arquivos do usuário) em vez de criar uma nova.
    VfsNode* legacy_home = vfs_resolve(vfs_root(), "/home/user");
    if (legacy_home && legacy_home->type == VFS_DIR) {
        copy_str(u.home, sizeof(u.home), "/home/user");
    } else {
        copy_str(u.home, sizeof(u.home), "/home/");
        kstrncat(u.home, name, sizeof(u.home));
    }

    if (is_hex_string(pwhash, 8)) {
        copy_str(u.salt_hex, sizeof(u.salt_hex), "fnv");
        copy_str(u.hash_hex, sizeof(u.hash_hex), pwhash);
    }

    add_root_entry();
    kmemcpy(&g_users[g_user_count++], &u, sizeof(u));

    VfsNode* home = vfs_resolve(vfs_root(), u.home);
    if (!home) home = vfs_mkdir(vfs_resolve(vfs_root(), "/home"), u.name);
    if (home && home->type == VFS_DIR) {
        vfs_chown_tree(home, u.uid, u.gid);
        vfs_set_owner(home, u.uid, u.gid, 0700);
    }

    // O arquivo antigo tinha o hash fraco — não deixa uma cópia dele no disco.
    vfs_rm(cfg);
    return true;
}

void user_init(void) {
    kmemset(g_users, 0, sizeof(g_users));
    kmemset(&g_session, 0, sizeof(g_session));
    g_user_count = 0;

    vfs_batch_begin();

    VfsNode* etc = ensure_dir(vfs_root(), "etc", VFS_MODE_DIR_DEFAULT, false);
    VfsNode* pw  = etc ? vfs_find_child(etc, "passwd") : NULL;

    if (pw && pw->type == VFS_FILE && pw->size > 0) {
        load_passwd(pw);
        add_root_entry();
        // Defesa em profundidade: mesmo que alguém tenha trocado o modo em
        // disco, os hashes nunca ficam legíveis por usuários comuns.
        vfs_set_owner(pw, 0, 0, 0600);
    } else {
        fix_system_dirs();
        etc = vfs_find_child(vfs_root(), "etc");
        bool migrated = etc && migrate_user_cfg(etc);
        if (migrated) save_passwd();
        else          add_root_entry();   // primeiro boot: só root, em memória;
                                          // /etc/passwd nasce com o 1º usuário
    }

    vfs_batch_end();
}

// ---- Autenticação ---------------------------------------------------------

// Política de tentativas: até LOGIN_FREE_TRIES falhas seguidas sem espera;
// a partir daí a conta fica bloqueada por 30 s, 60 s, 120 s... (máx. 15 min).
// Só em memória: reiniciar zera — o custo do PBKDF2 e o teclado físico
// continuam sendo a barreira principal.
#define LOGIN_FREE_TRIES   5u
#define LOGIN_LOCK_BASE_S  30u
#define LOGIN_LOCK_MAX_S   900u

static void register_failure(UserEntry* u) {
    u->fail_count++;
    if (u->fail_count >= LOGIN_FREE_TRIES) {
        uint32_t shift = u->fail_count - LOGIN_FREE_TRIES;
        if (shift > 5) shift = 5;
        uint32_t secs = LOGIN_LOCK_BASE_S << shift;
        if (secs > LOGIN_LOCK_MAX_S) secs = LOGIN_LOCK_MAX_S;
        u->locked_until = timer_ticks + (uint64_t)secs * 100u;   // 100 Hz
    }
}

int user_verify(const char* name, const char* password, const UserEntry** out) {
    if (out) *out = NULL;
    if (!name || !name[0]) return USER_ERR_BADPW;

    UserEntry* u = (UserEntry*)find_nocase(name);
    if (!u) {
        // Usuário inexistente: faz o MESMO trabalho (um PBKDF2) e devolve o
        // MESMO erro de senha errada — nem a mensagem nem o tempo revelam
        // se a conta existe.
        static const uint8_t dummy_salt[16] = {0};
        char hx[65];
        hash_password(password ? password : "", dummy_salt, hx);
        kmemset(hx, 0, sizeof(hx));
        return USER_ERR_BADPW;
    }
    if (u->flags & USER_FLAG_LOCKED) return USER_ERR_LOCKED;
    if (u->locked_until && timer_ticks < u->locked_until) return USER_ERR_THROTTLED;

    bool upgrade = false;
    int r = entry_check_password(u, password, &upgrade);
    if (r != USER_OK) {
        register_failure(u);
        return r;
    }

    u->fail_count = 0;
    u->locked_until = 0;
    if (upgrade) {                       // hash FNV antigo -> PBKDF2 com salt
        entry_set_password(u, password);
        save_passwd();
    }
    if (out) *out = u;
    return USER_OK;
}

// ---- Administração ----------------------------------------------------------

int user_create(const char* name, const char* password, uint32_t admin) {
    if (!name || !user_name_valid(name)) return USER_ERR_INVALID;
    if (password && kstrlen(password) > PASSWORD_MAX) return USER_ERR_INVALID;
    if (g_user_count >= USER_MAX) return USER_ERR_LIMIT;
    if (find_nocase(name)) return USER_ERR_EXISTS;

    uint32_t uid = UID_FIRST_USER;
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid >= uid) uid = g_users[i].uid + 1;

    UserEntry e;
    kmemset(&e, 0, sizeof(e));
    e.uid = uid;
    e.gid = uid;
    e.flags = admin ? USER_FLAG_ADMIN : 0u;
    copy_str(e.name, sizeof(e.name), name);
    copy_str(e.home, sizeof(e.home), "/home/");
    kstrncat(e.home, name, sizeof(e.home));
    entry_set_password(&e, password);

    vfs_batch_begin();

    VfsNode* homes = ensure_dir(vfs_root(), "home", VFS_MODE_DIR_DEFAULT, false);
    VfsNode* h = homes ? vfs_find_child(homes, name) : NULL;
    if (h && h->type != VFS_DIR) { vfs_batch_end(); return USER_ERR_EXISTS; }
    if (!h && homes) h = vfs_mkdir(homes, name);
    if (!h) { vfs_batch_end(); return USER_ERR_IO; }

    // Se sobrou uma home de uma conta removida antes, ela passa a ser desta.
    vfs_chown_tree(h, uid, uid);
    vfs_set_owner(h, uid, uid, 0700);

    if (!vfs_find_child(h, "README.txt")) {
        VfsNode* readme = vfs_touch(h, "README.txt");
        if (readme) {
            vfs_write(readme, tr(STR_USER_README_TEXT));
            vfs_set_owner(readme, uid, uid, VFS_MODE_FILE_DEFAULT);
        }
    }

    kmemcpy(&g_users[g_user_count], &e, sizeof(e));
    g_user_count++;

    bool ok = save_passwd();
    vfs_batch_end();
    kmemset(&e, 0, sizeof(e));

    if (!ok) {                                   // desfaz em memória
        g_user_count--;
        kmemset(&g_users[g_user_count], 0, sizeof(UserEntry));
        return USER_ERR_IO;
    }
    return USER_OK;
}

int user_set_password(uint32_t uid, const char* password) {
    if (password && kstrlen(password) > PASSWORD_MAX) return USER_ERR_INVALID;
    UserEntry* u = NULL;
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid == uid) { u = &g_users[i]; break; }
    if (!u) return USER_ERR_NOUSER;
    if (u->uid == UID_ROOT) return USER_ERR_DENIED;   // root é travada: sem senha

    entry_set_password(u, password);
    return save_passwd() ? USER_OK : USER_ERR_IO;
}

int user_delete(uint32_t uid) {
    if (uid == UID_ROOT) return USER_ERR_DENIED;
    if (g_session.active && g_session.user.uid == uid) return USER_ERR_DENIED;

    int idx = -1;
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid == uid) { idx = (int)i; break; }
    if (idx < 0) return USER_ERR_NOUSER;

    if (g_users[idx].flags & USER_FLAG_ADMIN) {      // nunca fica sem administrador
        uint32_t admins = 0;
        for (uint32_t i = 0; i < g_user_count; i++)
            if (g_users[i].flags & USER_FLAG_ADMIN) admins++;
        if (admins <= 1) return USER_ERR_DENIED;
    }

    vfs_batch_begin();
    // A home continua no disco (não apagamos dados do usuário sem ele pedir),
    // mas passa para root — assim, se esse uid for reaproveitado, a conta
    // nova NÃO herda os arquivos antigos.
    VfsNode* h = vfs_resolve(vfs_root(), g_users[idx].home);
    if (h && h != vfs_root()) vfs_chown_tree(h, UID_ROOT, UID_ROOT);

    for (uint32_t i = (uint32_t)idx; i + 1 < g_user_count; i++)
        kmemcpy(&g_users[i], &g_users[i + 1], sizeof(UserEntry));
    g_user_count--;
    kmemset(&g_users[g_user_count], 0, sizeof(UserEntry));

    bool ok = save_passwd();
    vfs_batch_end();
    return ok ? USER_OK : USER_ERR_IO;
}

// ---- Sessão ------------------------------------------------------------------

void session_start(const UserEntry* u) {
    if (!u) return;
    kmemcpy(&g_session.user, u, sizeof(UserEntry));
    g_session.active = true;

    // Home: usa a existente; se sumiu (disco editado, etc.), recria vazia.
    VfsNode* h = vfs_resolve(vfs_root(), u->home);
    if (!h) {
        VfsNode* homes = vfs_resolve(vfs_root(), "/home");
        if (homes && homes->type == VFS_DIR) {
            vfs_batch_begin();
            h = vfs_mkdir(homes, u->name);
            if (h) vfs_set_owner(h, u->uid, u->gid, 0700);
            vfs_batch_end();
        }
    }
    vfs_set_cwd(h && h->type == VFS_DIR ? h : vfs_root());
}

bool        session_active(void)    { return g_session.active; }
uint32_t    session_uid(void)       { return g_session.active ? g_session.user.uid : 65534u; }
uint32_t    session_gid(void)       { return g_session.active ? g_session.user.gid : 65534u; }
bool        session_is_admin(void)  { return g_session.active && (g_session.user.flags & USER_FLAG_ADMIN); }
const char* session_username(void)  { return g_session.active ? g_session.user.name : ""; }
const char* session_home(void)      { return g_session.active ? g_session.user.home : "/"; }

Cred session_cred(void) {
    Cred c;
    c.uid    = session_uid();
    c.gid    = session_gid();
    c.bypass = (session_is_admin() || c.uid == UID_ROOT) ? 1u : 0u;
    return c;
}

Cred cred_for_ids(uint32_t uid, uint32_t gid) {
    Cred c;
    c.uid    = uid;
    c.gid    = gid;
    c.bypass = (uid == UID_ROOT) ? 1u : 0u;
    return c;
}
