// tests/perm/test_perm.c — teste de HOST (não roda no HAOS) das permissões
// do VFS (fs/perm.c), do banco de usuários (kernel/user.c) e do SHA-256.
// Usa o vfs.c/perm.c/user.c REAIS; só disco, memória e relógio são stubs.
//
//   bash tests/perm/run.sh
#include <stdio.h>
#include <string.h>
#undef NULL
#include "../../kernel/types.h"
#include "../../fs/vfs.h"
#include "../../fs/perm.h"
#include "../../kernel/user.h"
#include "../../kernel/sha256.h"

extern volatile uint64_t timer_ticks;   // definido em stubs.c

extern int g_save_count;

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
    printf("  FALHOU (linha %d): %s\n", __LINE__, #cond); } } while (0)

static void fresh(void) { vfs_init(); user_init(); }

static VfsNode* at(const char* path) { return vfs_resolve(vfs_root(), path); }
static const char* content(VfsNode* f) { return (f && f->data) ? f->data : ""; }
static Cred user_cred(const char* name) {
    const UserEntry* u = user_find(name);
    return cred_for_ids(u ? u->uid : 9999, u ? u->gid : 9999);
}
static uint32_t fnv(const char* s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

// ---------------------------------------------------------------------
static void test_fresh_install(void) {
    printf("[1] primeiro boot / criação de contas\n");
    fresh();
    CHECK(user_total() == 1);                       // só root (em memória)
    CHECK(user_regular_count() == 0);
    CHECK(user_find("root") && (user_find("root")->flags & USER_FLAG_LOCKED));

    int before = g_save_count;
    CHECK(user_create("ana", "segredo", 1) == USER_OK);
    CHECK(g_save_count - before == 1);              // UMA gravação em disco, não 4-5
    CHECK(user_create("bob", "", 0) == USER_OK);
    CHECK(user_create("carol", "x", 0) == USER_OK);

    const UserEntry* ana = user_find("ana");
    CHECK(ana && ana->uid == 1000 && ana->gid == 1000 && user_is_admin(ana));
    CHECK(strcmp(ana->home, "/home/ana") == 0);
    CHECK(user_find("bob")->uid == 1001 && !user_is_admin(user_find("bob")));
    CHECK(user_has_password(ana) && !user_has_password(user_find("bob")));

    VfsNode* home = at("/home/ana");
    CHECK(home && home->type == VFS_DIR && home->uid == 1000 && home->mode == 0700);
    VfsNode* readme = at("/home/ana/README.txt");
    CHECK(readme && readme->uid == 1000 && readme->mode == 0644);

    VfsNode* pw = at("/etc/passwd");
    CHECK(pw && pw->uid == 0 && pw->mode == 0600);
    CHECK(strstr(content(pw), "segredo") == NULL);  // nunca a senha em claro
    CHECK(strstr(content(pw), "ana:1000:1000:A:/home/ana:") != NULL);
    CHECK(strstr(content(pw), "bob:1001:1001:-:/home/bob::") != NULL);   // sem senha: salt/hash vazios

    // nomes duplicados (sem diferenciar maiúsculas) e inválidos
    CHECK(user_create("ANA", "", 0) == USER_ERR_EXISTS);
    CHECK(user_create("root", "", 0) == USER_ERR_EXISTS);
    CHECK(user_create("", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("a b", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("a:b", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("a/b", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("..", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("-x", "", 0) == USER_ERR_INVALID);
    CHECK(user_create("nome_com_mais_de_trinta_e_um_caracteres_x", "", 0) == USER_ERR_INVALID);

    // autenticação
    const UserEntry* out = NULL;
    CHECK(user_verify("ana", "segredo", &out) == USER_OK && out == ana);
    CHECK(user_verify("ana", "errada", &out) == USER_ERR_BADPW && out == NULL);
    CHECK(user_verify("ana", "", NULL) == USER_ERR_BADPW);
    CHECK(user_verify("bob", "", NULL) == USER_OK);             // sem senha
    CHECK(user_verify("bob", "algo", NULL) == USER_ERR_BADPW);  // sem senha: só vazia
    CHECK(user_verify("root", "", NULL) == USER_ERR_LOCKED);    // ninguém entra como root
    CHECK(user_verify("fantasma", "", NULL) == USER_ERR_BADPW);    // igual a senha errada: não revela se existe

    // salts diferentes => mesma senha, hashes diferentes
    CHECK(user_set_password(1001, "igual") == USER_OK);
    CHECK(user_set_password(1002, "igual") == USER_OK);
    CHECK(strcmp(user_find("bob")->hash_hex, user_find("carol")->hash_hex) != 0);
    CHECK(strcmp(user_find("bob")->salt_hex, user_find("carol")->salt_hex) != 0);
    CHECK(user_verify("bob", "igual", NULL) == USER_OK);
    CHECK(user_set_password(1001, "") == USER_OK && !user_has_password(user_find("bob")));
    CHECK(user_set_password(0, "x") == USER_ERR_DENIED);        // root travada
    CHECK(user_set_password(4242, "x") == USER_ERR_NOUSER);
}

static void test_delete_rules(void) {
    printf("[2] remoção de contas\n");
    fresh();
    user_create("ana", "a", 1);
    user_create("bob", "b", 0);

    CHECK(user_delete(UID_ROOT) == USER_ERR_DENIED);
    CHECK(user_delete(1000) == USER_ERR_DENIED);        // último administrador
    CHECK(user_create("dan", "d", 1) == USER_OK);
    session_start(user_find("dan"));
    CHECK(user_delete(user_find("dan")->uid) == USER_ERR_DENIED);   // conta da sessão
    CHECK(user_delete(1000) == USER_OK);                // agora há outro administrador

    // a home continua no disco, mas passa para root; o uid pode ser reaproveitado sem herdar nada
    VfsNode* h = at("/home/ana");
    CHECK(h && h->uid == 0);
    CHECK(at("/home/ana/README.txt") && at("/home/ana/README.txt")->uid == 0);
    CHECK(user_find("ana") == NULL);
    CHECK(user_create("eve", "e", 0) == USER_OK);
    CHECK(user_find("eve")->uid == 1003);               // não reusa 1000 enquanto houver uid maior
    CHECK(user_delete(9999) == USER_ERR_NOUSER);
}

static void test_permissions(void) {
    printf("[3] permissões do VFS\n");
    fresh();
    user_create("ana", "a", 1);
    user_create("bob", "b", 0);
    Cred ana = user_cred("ana"), bob = user_cred("bob");
    Cred admin = ana; admin.bypass = 1;
    VfsNode* n = NULL;

    // atravessar: README 0644 dentro de home 0700 é inacessível para outros
    CHECK(vfs_resolve_as(&ana, vfs_root(), "/home/ana/README.txt", &n) == PERM_OK && n);
    CHECK(vfs_resolve_as(&bob, vfs_root(), "/home/ana/README.txt", &n) == PERM_EACCES && n == NULL);
    CHECK(vfs_resolve_as(&bob, vfs_root(), "/home/ana", &n) == PERM_OK);      // ver o NOME da pasta pode (/home é 0755)
    CHECK(!perm_allows(n, &bob, PERM_R) && !perm_allows(n, &bob, PERM_X));    // mas não listar nem entrar
    CHECK(perm_allows(n, &admin, PERM_R | PERM_W | PERM_X));                  // administrador ignora
    CHECK(vfs_resolve_as(&bob, vfs_root(), "/home/ana/../bob/README.txt", &n) == PERM_EACCES); // ".." também exige X
    CHECK(vfs_resolve_as(&bob, vfs_root(), "/nao/existe", &n) == PERM_ENOENT);

    // /etc/passwd: ninguém comum lê
    VfsNode* pw = at("/etc/passwd");
    CHECK(!perm_allows(pw, &bob, PERM_R) && !perm_allows(pw, &ana, PERM_R));
    CHECK(perm_allows(pw, &admin, PERM_R));

    // criar: não em /etc nem /home, sim na própria home e em /tmp
    int err = 0;
    CHECK(vfs_touch_as(&bob, at("/etc"), "x", &err) == NULL && err == PERM_EACCES);
    CHECK(vfs_mkdir_as(&bob, at("/home"), "y", &err) == NULL && err == PERM_EACCES);
    CHECK(vfs_touch_as(&bob, at("/home/ana"), "z", &err) == NULL && err == PERM_EACCES);
    VfsNode* mine = vfs_touch_as(&bob, at("/home/bob"), "meu.txt", &err);
    CHECK(mine && mine->uid == 1001 && mine->mode == 0644);
    CHECK(vfs_touch_as(&bob, at("/home/bob"), "meu.txt", &err) == mine);      // existente: devolve o mesmo
    VfsNode* dir = vfs_mkdir_as(&bob, at("/home/bob"), "sub", &err);
    CHECK(dir && dir->uid == 1001 && dir->mode == 0755);

    // /tmp: todos criam; sticky => só o dono (ou administrador) remove
    VfsNode* tmp = at("/tmp");
    CHECK(tmp->mode == (0777 | VFS_MODE_STICKY));
    VfsNode* t1 = vfs_touch_as(&ana, tmp, "da_ana", &err);
    VfsNode* t2 = vfs_touch_as(&bob, tmp, "do_bob", &err);
    CHECK(t1 && t2);
    CHECK(vfs_remove_as(&bob, t1) == PERM_EACCES && !t1->removed);   // bob não apaga o da ana
    CHECK(vfs_remove_as(&bob, t2) == PERM_OK && t2->removed);        // o próprio, sim
    CHECK(vfs_remove_as(&admin, t1) == PERM_OK && t1->removed);      // administrador, sim

    // remover recursivo não pode apagar o que o usuário não poderia apagar
    VfsNode* d = vfs_mkdir_as(&bob, tmp, "pasta", &err);
    vfs_chmod_as(&bob, d, 0777 | VFS_MODE_STICKY);
    VfsNode* inside = vfs_touch_as(&ana, d, "da_ana_dentro", &err);
    CHECK(inside != NULL);
    CHECK(vfs_remove_as(&bob, d) == PERM_OK);   // dono do dir sticky remove tudo (regra do Unix: dono do diretório)
    CHECK(inside->removed);
    VfsNode* d2 = vfs_mkdir_as(&ana, tmp, "pasta2", &err);
    vfs_chmod_as(&ana, d2, 0777 | VFS_MODE_STICKY);
    VfsNode* inside2 = vfs_touch_as(&bob, d2, "do_bob_dentro", &err);
    CHECK(inside2 != NULL);
    CHECK(vfs_remove_as(&bob, d2) == PERM_EACCES && !d2->removed && !inside2->removed); // não é dono da pasta
    VfsNode* d3 = vfs_mkdir_as(&ana, tmp, "pasta3", &err);       // 0755: bob nem pode criar dentro
    CHECK(vfs_touch_as(&bob, d3, "x", &err) == NULL && err == PERM_EACCES);

    // Escalonamento clássico: bob cria /tmp/D (0777), ana cria D/E (0755, só dela)
    // com um arquivo dentro. bob pode remover a entrada E de D (tem W em D), mas
    // NÃO pode apagar o conteúdo de E — então remover D inteiro tem de ser negado.
    VfsNode* D = vfs_mkdir_as(&bob, tmp, "D", &err);
    vfs_chmod_as(&bob, D, 0777);
    VfsNode* E = vfs_mkdir_as(&ana, D, "E", &err);
    VfsNode* F = vfs_touch_as(&ana, E, "importante.txt", &err);
    CHECK(D && E && F);
    CHECK(vfs_remove_as(&bob, D) == PERM_EACCES);
    CHECK(!D->removed && !E->removed && !F->removed);       // nada foi apagado
    CHECK(vfs_remove_as(&bob, E) == PERM_EACCES);           // E é 0755 de ana: bob não apaga o que há dentro
    CHECK(vfs_remove_as(&ana, D) == PERM_EACCES);           // /tmp é sticky e D é de bob: ana não remove D
    CHECK(vfs_remove_as(&ana, E) == PERM_OK && E->removed && F->removed);   // ana remove o que é dela
    CHECK(vfs_remove_as(&bob, D) == PERM_OK && D->removed); // agora D está vazio: bob remove

    // raiz nunca é removível
    CHECK(vfs_remove_as(&admin, vfs_root()) == PERM_EINVAL);

    // arquivos: R / W separados
    VfsNode* f = vfs_touch_as(&ana, at("/home/ana"), "f.txt", &err);
    CHECK(perm_allows(f, &ana, PERM_R | PERM_W));
    vfs_chmod_as(&ana, f, 0444);
    CHECK(perm_allows(f, &ana, PERM_R) && !perm_allows(f, &ana, PERM_W));    // classe "dono" vale, mesmo que "outros" tivesse mais
    vfs_chmod_as(&ana, f, 0007);
    CHECK(!perm_allows(f, &ana, PERM_R));                                    // dono manda: 0007 não dá nada ao dono
    CHECK(perm_allows(f, &bob, PERM_R | PERM_W | PERM_X));

    // chmod / chown
    CHECK(vfs_chmod_as(&bob, f, 0777) == PERM_EACCES);                       // não é o dono
    CHECK(vfs_chmod_as(&ana, f, 0600) == PERM_OK && f->mode == 0600);
    CHECK(vfs_chmod_as(&ana, f, 07777) == PERM_OK && f->mode == 01777);      // máscara: só 01777
    CHECK(vfs_chown_as(&ana, f, 1001, 1001) == PERM_EACCES);                 // dono comum não dá o arquivo a outro
    CHECK(vfs_chown_as(&admin, f, 1001, 1001) == PERM_OK && f->uid == 1001);

    // nó removido nunca é acessível
    CHECK(!perm_allows(t1, &admin, PERM_R));
}

static void test_tree_chown_and_format(void) {
    printf("[4] chown recursivo e formato ls -l\n");
    fresh();
    user_create("ana", "", 1);
    VfsNode* tmp = at("/tmp");
    char pm[11];
    perm_format(tmp, pm);                     CHECK(strcmp(pm, "drwxrwxrwt") == 0);
    perm_format(at("/home/ana"), pm);         CHECK(strcmp(pm, "drwx------") == 0);
    perm_format(at("/home/ana/README.txt"), pm); CHECK(strcmp(pm, "-rw-r--r--") == 0);
    perm_format(at("/etc/passwd"), pm);       CHECK(strcmp(pm, "-rw-------") == 0);
    vfs_set_owner(at("/bin"), 0, 0, 01000);   // sticky sem x em "outros" => 'T'
    perm_format(at("/bin"), pm);              CHECK(strcmp(pm, "d--------T") == 0);

    vfs_chown_tree(at("/home/ana"), 7, 8);
    CHECK(at("/home/ana")->uid == 7 && at("/home/ana/README.txt")->gid == 8);
    CHECK(at("/home/ana")->mode == 0700);     // chown_tree não mexe nas permissões
}

static void test_batch(void) {
    printf("[5] lote de gravações\n");
    fresh();
    int s0 = g_save_count;
    vfs_batch_begin();
    vfs_batch_begin();                         // aninhado
    VfsNode* a = vfs_mkdir(vfs_root(), "lote");
    vfs_touch(a, "1"); vfs_touch(a, "2");
    CHECK(g_save_count == s0);                 // nada gravado durante o lote
    vfs_batch_end();
    CHECK(g_save_count == s0);                 // lote externo ainda aberto
    CHECK(vfs_batch_end());
    CHECK(g_save_count == s0 + 1);             // exatamente uma gravação ao fechar
    vfs_batch_begin(); vfs_batch_end();
    CHECK(g_save_count == s0 + 1);             // lote vazio não grava
    vfs_batch_end();                           // desbalanceado: não quebra
    vfs_mkdir(vfs_root(), "fora");
    CHECK(g_save_count == s0 + 2);             // fora do lote volta a gravar na hora
}

static void test_migration(void) {
    printf("[6] migração do /etc/user.cfg antigo (FNV-1a)\n");
    fresh();
    // monta o estado de um disco da versão anterior
    VfsNode* user = vfs_mkdir(at("/home"), "user");
    vfs_touch(user, "nota.txt");
    char cfg[128]; char hex[9];
    snprintf(hex, sizeof(hex), "%08x", fnv("abc"));
    snprintf(cfg, sizeof(cfg), "name=Ana Maria\npwhash=%s\n", hex);
    vfs_write(vfs_touch(at("/etc"), "user.cfg"), cfg);

    user_init();
    const UserEntry* u = user_find("Ana_Maria");               // espaço virou '_'
    CHECK(u != NULL);
    CHECK(u && u->uid == 1000 && user_is_admin(u));
    CHECK(u && strcmp(u->home, "/home/user") == 0);            // mantém a pasta antiga
    CHECK(at("/home/user")->uid == 1000 && at("/home/user")->mode == 0700);
    CHECK(at("/home/user/nota.txt")->uid == 1000);             // conteúdo passou para o dono
    CHECK(at("/etc/user.cfg") == NULL);                        // arquivo antigo (hash fraco) removido
    CHECK(at("/etc/passwd") && at("/etc/passwd")->mode == 0600);
    CHECK(u && strcmp(u->salt_hex, "fnv") == 0);               // ainda hash legado

    CHECK(user_verify("Ana_Maria", "errada", NULL) == USER_ERR_BADPW);
    CHECK(strcmp(user_find("Ana_Maria")->salt_hex, "fnv") == 0);   // erro não migra
    CHECK(user_verify("Ana_Maria", "abc", NULL) == USER_OK);
    u = user_find("Ana_Maria");
    CHECK(strcmp(u->salt_hex, "fnv") != 0 && strlen(u->salt_hex) == 32 && strlen(u->hash_hex) == 64);
    CHECK(strstr(content(at("/etc/passwd")), ":fnv:") == NULL);    // hash fraco saiu do disco
    CHECK(user_verify("Ana_Maria", "abc", NULL) == USER_OK);       // e continua funcionando

    // migração sem senha
    fresh();
    vfs_write(vfs_touch(at("/etc"), "user.cfg"), "name=Zé\n");
    user_init();
    CHECK(user_find("Zé") && !user_has_password(user_find("Zé")));
    CHECK(strcmp(user_find("Zé")->home, "/home/Zé") == 0);
    CHECK(at("/home/Zé") && at("/home/Zé")->uid == 1000);

    // reinício depois de configurado: passwd é recarregado
    user_init();
    CHECK(user_find("Zé") != NULL && user_regular_count() == 1);
}

static void test_passwd_parsing(void) {
    printf("[7] /etc/passwd corrompido ou editado\n");
    fresh();
    user_create("ana", "pw", 1);
    user_create("bob", "", 0);
    VfsNode* pw = at("/etc/passwd");
    // acrescenta lixo, duplicatas e linhas com campos inválidos
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s%s", content(pw),
        "lixo sem dois pontos\n"
        "# comentario\n"
        "ana:1500:1500:-:/home/ana::\n"            // nome duplicado
        "zed:1001:1001:-:/home/zed::\n"            // uid duplicado
        "mal:abc:1:-:/home/mal::\n"                // uid inválido
        "mal2:1600:1600:X:/home/mal2::\n"          // flag desconhecida
        "mal3:1601:1601:-:relativo::\n"            // home relativa
        "mal4:1602:1602:-:/home/mal4:zz:yy\n"      // salt/hash não-hex
        "bom:1700:1700:-:/home/bom::\n");
    vfs_write(pw, buf);
    user_init();
    CHECK(user_find("ana") && user_find("ana")->uid == 1000);   // a primeira vale
    CHECK(user_find("zed") == NULL && user_find("mal") == NULL && user_find("mal2") == NULL);
    CHECK(user_find("mal3") == NULL && user_find("mal4") == NULL);
    CHECK(user_find("bom") != NULL && user_find("bom")->uid == 1700);
    CHECK(user_find("root") != NULL);                           // root sempre existe
    CHECK(user_verify("ana", "pw", NULL) == USER_OK);

    // modo do passwd é reforçado mesmo se alguém o abriu no disco
    vfs_set_owner(at("/etc/passwd"), 1001, 1001, 0666);
    user_init();
    CHECK(at("/etc/passwd")->uid == 0 && at("/etc/passwd")->mode == 0600);
}

static void test_session(void) {
    printf("[8] sessão\n");
    fresh();
    user_create("ana", "", 1);
    user_create("bob", "", 0);
    CHECK(!session_active() && strcmp(session_username(), "") == 0 && strcmp(session_home(), "/") == 0);
    Cred n = session_cred();
    CHECK(n.uid == 65534 && n.bypass == 0);

    session_start(user_find("bob"));
    CHECK(session_active() && session_uid() == 1001 && !session_is_admin());
    CHECK(vfs_cwd() == at("/home/bob"));
    Cred c = session_cred();
    CHECK(c.uid == 1001 && c.gid == 1001 && c.bypass == 0);

    session_start(user_find("ana"));
    c = session_cred();
    CHECK(session_is_admin() && c.bypass == 1 && vfs_cwd() == at("/home/ana"));

    // home sumiu: é recriada vazia e com o dono certo
    vfs_rm(at("/home/bob"));
    session_start(user_find("bob"));
    CHECK(at("/home/bob") && at("/home/bob")->uid == 1001 && vfs_cwd() == at("/home/bob"));

    // processo: uid 0 é o único com bypass; uid de usuário nunca
    CHECK(cred_for_ids(0, 0).bypass == 1 && cred_for_ids(1000, 1000).bypass == 0);
}

static void test_login_hardening(void) {
    printf("[9] login: maiúsculas, enumeração, bloqueio\n");
    fresh();
    user_create("ana", "pw", 1);
    const UserEntry* out = NULL;

    CHECK(user_find("ANA") == user_find("ana") && user_find("ana"));
    CHECK(user_create("ANA", "x", 0) == USER_ERR_EXISTS);
    CHECK(user_verify("ANA", "pw", &out) == USER_OK && out == user_find("ana"));
    CHECK(user_verify("nao_existe", "pw", NULL) == user_verify("ana", "errada", NULL));

    // 5 falhas seguidas -> 6ª tentativa é barrada mesmo com a senha CERTA
    // (a comparação acima já gastou a 1ª falha; estas são a 2ª, 3ª, 4ª e 5ª)
    for (int i = 0; i < 4; i++) CHECK(user_verify("ana", "errada", NULL) == USER_ERR_BADPW);
    CHECK(user_verify("ana", "pw", NULL) == USER_ERR_THROTTLED);
    timer_ticks += 29 * 100;
    CHECK(user_verify("ana", "pw", NULL) == USER_ERR_THROTTLED);   // ainda dentro dos 30 s
    timer_ticks += 2 * 100;
    CHECK(user_verify("ana", "pw", NULL) == USER_OK);               // passou: entra e zera
    CHECK(user_verify("ana", "errada", NULL) == USER_ERR_BADPW);    // contador recomeçou
    CHECK(user_verify("ana", "pw", NULL) == USER_OK);
}

static void test_rename_copy(void) {
    printf("[10] mover, renomear e copiar\n");
    fresh();
    user_create("ana", "x", 0);
    user_create("bob", "x", 0);
    Cred a = user_cred("ana"), b = user_cred("bob");
    VfsNode* ahome = at("/home/ana");
    VfsNode* bhome = at("/home/bob");
    int e = 0;

    VfsNode* f1 = vfs_touch_as(&a, ahome, "a.txt", &e);
    CHECK(f1 && vfs_write(f1, "conteudo"));

    // renomear na mesma pasta: UMA gravação, conteúdo e dono intactos
    int before = g_save_count;
    CHECK(vfs_rename_as(&a, f1, ahome, "b.txt") == PERM_OK);
    CHECK(g_save_count - before == 1);
    CHECK(at("/home/ana/b.txt") == f1 && !at("/home/ana/a.txt"));
    CHECK(strcmp(content(f1), "conteudo") == 0 && f1->uid == a.uid);

    // mover para uma subpasta
    VfsNode* d = vfs_mkdir_as(&a, ahome, "d", &e);
    CHECK(d != NULL);
    CHECK(vfs_rename_as(&a, f1, d, "b.txt") == PERM_OK);
    CHECK(at("/home/ana/d/b.txt") == f1 && !at("/home/ana/b.txt"));
    CHECK(f1->parent == d && d->child_count == 1 && ahome->child_count >= 1);

    // não sobrescreve
    VfsNode* x = vfs_touch_as(&a, ahome, "x.txt", &e);
    CHECK(x != NULL);
    CHECK(vfs_rename_as(&a, f1, ahome, "x.txt") == PERM_EINVAL);
    CHECK(at("/home/ana/d/b.txt") == f1);                    // continua onde estava

    // nomes inválidos
    CHECK(vfs_rename_as(&a, f1, d, "") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, f1, d, ".") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, f1, d, "..") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, f1, d, "a/b") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, f1, d, "b.txt") == PERM_OK);     // mesmo nome: sem efeito

    // pasta dentro de si mesma / de um descendente
    VfsNode* sub = vfs_mkdir_as(&a, d, "sub", &e);
    CHECK(sub != NULL);
    CHECK(vfs_rename_as(&a, d, d, "d") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, d, sub, "d") == PERM_EINVAL);
    CHECK(vfs_rename_as(&a, vfs_root(), ahome, "r") == PERM_EINVAL);    // raiz

    // permissões: bob não mexe em /home/ana (0700)
    CHECK(vfs_rename_as(&b, f1, d, "roubado") == PERM_EACCES);
    CHECK(vfs_rename_as(&b, f1, bhome, "roubado") == PERM_EACCES);
    CHECK(vfs_rename_as(&a, f1, bhome, "presente") == PERM_EACCES);     // sem W+X no destino
    CHECK(at("/home/ana/d/b.txt") == f1);

    // sticky (/tmp): bob não renomeia o arquivo da ana
    VfsNode* tmp = at("/tmp");
    CHECK(tmp != NULL);
    if (tmp) {
        VfsNode* tf = vfs_touch_as(&a, tmp, "f", &e);
        CHECK(tf != NULL);
        CHECK(vfs_rename_as(&b, tf, tmp, "g") == PERM_EACCES);
        CHECK(vfs_rename_as(&a, tf, tmp, "g") == PERM_OK);
    }

    // cópia de pasta: árvore independente, dono = quem copiou, UMA gravação
    VfsNode* inner = at("/home/ana/d/b.txt");
    before = g_save_count;
    VfsNode* d2 = NULL;
    CHECK(vfs_copy_as(&a, d, ahome, "d2", &d2) == PERM_OK && d2);
    CHECK(g_save_count - before == 1);
    VfsNode* c = at("/home/ana/d2/b.txt");
    CHECK(c && c != inner && strcmp(content(c), "conteudo") == 0);
    CHECK(c && c->data != inner->data && c->uid == a.uid);
    CHECK(at("/home/ana/d2/sub") != NULL);
    vfs_write(c, "mudou");
    CHECK(strcmp(content(inner), "conteudo") == 0);                     // original intacto

    // cópia: sem sobrescrever, pasta dentro de si mesma, nome inválido
    CHECK(vfs_copy_as(&a, d, ahome, "d2", NULL) == PERM_EINVAL);
    CHECK(vfs_copy_as(&a, d, sub, "dentro", NULL) == PERM_EINVAL);
    CHECK(!at("/home/ana/d/sub/dentro"));
    CHECK(vfs_copy_as(&a, d, ahome, "a/b", NULL) == PERM_EINVAL);

    // cópia respeita leitura e escrita
    CHECK(vfs_copy_as(&b, inner, bhome, "c.txt", NULL) == PERM_OK);     // arquivo 0644: bob pode ler
    CHECK(at("/home/bob/c.txt") && at("/home/bob/c.txt")->uid == b.uid);
    CHECK(vfs_chmod_as(&a, inner, 0600) == PERM_OK);
    CHECK(vfs_copy_as(&b, inner, bhome, "c2.txt", NULL) == PERM_EACCES); // agora 0600: não
    CHECK(vfs_copy_as(&b, d2, ahome, "d3", NULL) == PERM_EACCES);        // sem W+X no destino
    CHECK(!at("/home/ana/d3") && !at("/home/bob/c2.txt"));

    // cópia de arquivo preserva o modo
    VfsNode* priv = vfs_touch_as(&a, ahome, "priv.txt", &e);
    CHECK(priv && vfs_write(priv, "s") && vfs_chmod_as(&a, priv, 0600) == PERM_OK);
    CHECK(vfs_copy_as(&a, priv, ahome, "priv2.txt", NULL) == PERM_OK);
    CHECK(at("/home/ana/priv2.txt") && at("/home/ana/priv2.txt")->mode == 0600);

    // vfs_resolve_parent_as
    VfsNode* par = NULL;
    char nm[VFS_NAME_MAX];
    CHECK(vfs_resolve_parent_as(&a, vfs_root(), "/home/ana/novo.txt", &par, nm) == PERM_OK);
    CHECK(par == ahome && strcmp(nm, "novo.txt") == 0);
    CHECK(vfs_resolve_parent_as(&a, ahome, "rel", &par, nm) == PERM_OK && par == ahome && strcmp(nm, "rel") == 0);
    CHECK(vfs_resolve_parent_as(&a, vfs_root(), "/home/ana/d/", &par, nm) == PERM_OK);
    CHECK(par == ahome && strcmp(nm, "d") == 0);                        // '/' final é ignorada
    CHECK(vfs_resolve_parent_as(&a, vfs_root(), "/", &par, nm) == PERM_EINVAL);
    CHECK(vfs_resolve_parent_as(&a, vfs_root(), "/nada/x", &par, nm) != PERM_OK);
    CHECK(vfs_resolve_parent_as(&b, vfs_root(), "/home/ana/x", &par, nm) == PERM_EACCES);
}

int main(void) {
    test_fresh_install();
    test_delete_rules();
    test_permissions();
    test_tree_chown_and_format();
    test_batch();
    test_migration();
    test_passwd_parsing();
    test_session();
    test_login_hardening();
    test_rename_copy();
    printf("\n%d verificacoes, %d falha(s)\n", checks, fails);
    return fails ? 1 : 0;
}
