#include "vfs.h"
#include "../kernel/memory.h"
#include "haosfs.h"
#include "../drivers/storage.h"
#include "../kernel/generated/hello_hexec_data.h"
#include "../kernel/generated/calc_hexec_data.h"

// winpaint (janela gráfica Ring 3) só entra se o header GERADO existir —
// ele é criado por `bash tools/build_apps.sh`. Assim o kernel compila
// mesmo antes de os apps terem sido gerados.
#if defined(__has_include)
#  if __has_include("../kernel/generated/winpaint_hexec_data.h")
#    include "../kernel/generated/winpaint_hexec_data.h"
#    define HAVE_WINPAINT_HEXEC 1
#  endif
#endif
// Sistema de arquivos virtual que roda na RAM do OS.
// Arquivos crescem dinamicamente (dobra de capacidade) até VFS_FILE_MAX,
// e diretórios suportam até VFS_MAX_CHILDREN itens.
// Se deletar tem uma "chance" de vazar memória (limitação conhecida, ok para um SO educacional).


// ---- Estado global ------------------------------------------
static VfsNode* g_root = NULL;
static VfsNode* g_cwd  = NULL;
static bool g_persistent = false;

static int  g_batch_depth = 0;
static bool g_batch_dirty = false;

static bool vfs_persist(void) {
    if (!g_persistent) return true;
    if (g_batch_depth > 0) { g_batch_dirty = true; return true; }
    return haosfs_save(storage_device(), g_root);
}

void vfs_batch_begin(void) { g_batch_depth++; }

bool vfs_batch_end(void) {
    if (g_batch_depth > 0) g_batch_depth--;
    if (g_batch_depth == 0 && g_batch_dirty) {
        g_batch_dirty = false;
        return g_persistent ? haosfs_save(storage_device(), g_root) : true;
    }
    return true;
}

void vfs_set_owner(VfsNode* node, uint32_t uid, uint32_t gid, uint16_t mode) {
    if (!node) return;
    node->uid  = uid;
    node->gid  = gid;
    node->mode = (uint16_t)(mode & VFS_MODE_MASK);
}

// ---- Helpers internos ---------------------------------------

static VfsNode* vfs_alloc_node(const char* name, VfsType type) {
    VfsNode* n = (VfsNode*)kzalloc(sizeof(VfsNode));
    if (!n) return NULL;
    // Cópia LIMITADA: `name` pode vir de um processo de usuário (sys_open
    // com O_CREAT, sys_mkdir) e ter até centenas de bytes — um kstrcpy
    // sem limite estourava `name[VFS_NAME_MAX]` e sobrescrevia type,
    // size e o ponteiro `data` do próprio nó (escrita arbitrária no
    // kernel a partir de Ring 3). kzalloc já zerou o nó, então o
    // último byte de `name` continua '\0'. Os chamadores públicos
    // (vfs_mkdir/vfs_touch) já rejeitam nomes longos; isto é a
    // segunda camada de defesa.
    kstrncpy(n->name, name, VFS_NAME_MAX - 1);
    n->type = type;
    n->size = 0;
    n->capacity = 0;
    n->data = NULL;
    n->fs_inode = 0;
    // Padrão: dono root. Código que age em nome de um usuário usa
    // vfs_mkdir_as/vfs_touch_as (fs/perm.h), que ajustam o dono.
    n->uid  = 0;
    n->gid  = 0;
    n->mode = (type == VFS_DIR) ? VFS_MODE_DIR_DEFAULT : VFS_MODE_FILE_DEFAULT;
    n->parent = NULL;
    n->child_count = 0;
    return n;
}

static bool vfs_add_child(VfsNode* parent, VfsNode* child) {
    if (!parent || !child) return false;
    if (parent->type != VFS_DIR) return false;
    if (parent->child_count >= VFS_MAX_CHILDREN) return false;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return true;
}

// ---- API pública --------------------------------------------

// ---- Executáveis embutidos em /bin ---------------------------
//
// Cada entrada vem de um header GERADO (kernel/generated/*_hexec_data.h
// — artefato de build criado por tools/embed_hexec.py a partir de um
// .hexec; nunca edite esses .h à mão: edite examples/<nome>/main.c e
// regenere, ver libc/README.md). Escritos byte a byte via
// vfs_ensure_capacity + kmemcpy (não vfs_write, que corta no primeiro
// '\0' — inaceitável para código de máquina). Rode com
// "run <nome>.hexec" no terminal.
//
//   hello     — exemplo mínimo da libc
//   calc      — calculadora (scanf)
//   winpaint  — janela gráfica Ring 3 (SYS_WIN_*); só se o header existir
typedef struct {
    const char*          name;
    const unsigned char* data;
    uint32_t             size;
} BuiltinBin;

static const BuiltinBin g_builtin_bins[] = {
    { "hello.hexec",  hello_hexec_data,  HELLO_HEXEC_SIZE  },
    { "calc.hexec",   calc_hexec_data,   CALC_HEXEC_SIZE   },
#ifdef HAVE_WINPAINT_HEXEC
    { "winpaint.hexec", winpaint_hexec_data, WINPAINT_HEXEC_SIZE },
#endif
};

static bool bytes_equal(const void* a, const void* b, uint32_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (uint32_t i = 0; i < n; i++) if (x[i] != y[i]) return false;
    return true;
}

// Instala os executáveis embutidos em `bin`, criando os que faltam e
// SUBSTITUINDO os que existem com conteúdo diferente (versão nova do
// kernel); os que já estão idênticos não são tocados. Retorna true se
// algo foi escrito (quem chama decide quando gravar no disco).
//
// Sem isto, um programa embutido novo só apareceria em discos
// formatados DEPOIS dele existir: vfs_create_defaults só roda na
// primeira formatação, e um disco já montado nunca recebia /bin
// novos nem versões corrigidas dos antigos. Consequência: qualquer
// edição manual em /bin/<nome embutido>.hexec é sobrescrita no boot —
// use outro nome (ou outro diretório) para programas seus.
static bool vfs_install_builtin_bins(VfsNode* bin) {
    bool changed = false;
    for (uint32_t i = 0; i < sizeof(g_builtin_bins) / sizeof(g_builtin_bins[0]); i++) {
        const BuiltinBin* b = &g_builtin_bins[i];
        VfsNode* f = vfs_find_child(bin, b->name);
        if (f && f->type != VFS_FILE) continue; // há um diretório com esse nome: não mexe
        if (f && f->size == b->size && f->data && bytes_equal(f->data, b->data, b->size)) continue;
        if (!f) f = vfs_touch(bin, b->name);
        if (!f || !vfs_ensure_capacity(f, b->size)) continue;
        kmemcpy(f->data, b->data, b->size);
        f->size = b->size;
        changed = true;
    }
    return changed;
}

static void vfs_create_defaults(void) {
    VfsNode* bin  = vfs_mkdir(g_root, "bin");
    VfsNode* home = vfs_mkdir(g_root, "home");
    VfsNode* etc  = vfs_mkdir(g_root, "etc");
    VfsNode* tmp  = vfs_mkdir(g_root, "tmp");

    (void)home;

    // Todos os diretórios de sistema são de root. /tmp é gravável por
    // todos (0777) com o bit "sticky" (01000): cada usuário só consegue
    // remover os PRÓPRIOS arquivos lá. Cada usuário ganha o seu
    // /home/<nome> quando a conta é criada (kernel/user.c).
    vfs_set_owner(g_root, 0, 0, VFS_MODE_DIR_DEFAULT);
    vfs_set_owner(bin,    0, 0, VFS_MODE_DIR_DEFAULT);
    vfs_set_owner(etc,    0, 0, VFS_MODE_DIR_DEFAULT);
    vfs_set_owner(tmp,    0, 0, 0777 | VFS_MODE_STICKY);

    // Executáveis embutidos no kernel (ver g_builtin_bins acima).
    vfs_install_builtin_bins(bin);

    // /etc/haos.conf
    VfsNode* conf = vfs_touch(etc, "haos.conf");
    vfs_write(conf, "# HAOS configuracao\nversion=1.1\ntheme=dark\n");

    // Sem usuário ainda: o CWD inicial é a raiz. O login define o CWD
    // como a home do usuário que entrar.
    g_cwd = g_root;
}

void vfs_init(void) {
    g_persistent = false;
    g_root = vfs_alloc_node("/", VFS_DIR);
    g_cwd  = g_root;

    BlockDevice* device = storage_device();
    HaosFsProbe probe = haosfs_probe(device);
    if (probe == HAOSFS_VALID && haosfs_mount(device, g_root)) {
        // Atualiza /bin com os executáveis embutidos nesta versão do
        // kernel ANTES de ligar a persistência: vfs_touch/vfs_mkdir
        // gravam o FS INTEIRO a cada chamada quando g_persistent está
        // ligado — com a persistência desligada fazemos todas as
        // mudanças em RAM e gravamos UMA vez no fim, e só se algo
        // mudou (o caso comum, boot sem novidades, não escreve nada).
        VfsNode* bin = vfs_find_child(g_root, "bin");
        if (!bin) bin = vfs_mkdir(g_root, "bin");
        bool changed = (bin && bin->type == VFS_DIR) && vfs_install_builtin_bins(bin);

        g_persistent = true;
        if (changed) vfs_persist();
        // O diretório de trabalho inicial é a raiz; quem define o cwd real é
        // session_start() (kernel/user.c), que usa a home do usuário que entrou.
        g_cwd = g_root;
        return;
    }

    if (probe == HAOSFS_EMPTY && haosfs_format(device)) {
        g_persistent = true;
        vfs_create_defaults();
        vfs_persist();
        return;
    }

    vfs_create_defaults();
}

VfsNode* vfs_root(void) { return g_root; }
VfsNode* vfs_cwd(void)  { return g_cwd;  }
void vfs_set_cwd(VfsNode* node) { if (node && node->type == VFS_DIR) g_cwd = node; }

VfsNode* vfs_find_child(VfsNode* dir, const char* name) {
    if (!dir || dir->type != VFS_DIR) return NULL;
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (kstrcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

// Resolve caminho absoluto ou relativo
VfsNode* vfs_resolve(VfsNode* base, const char* path) {
    if (!path || !path[0]) return base;

    VfsNode* cur;
    const char* p = path;

    if (p[0] == '/') {
        cur = g_root;
        p++;
    } else {
        cur = base ? base : g_cwd;
    }

    // Percorre componentes separados por '/'
    char component[VFS_NAME_MAX];
    while (*p) {
        // Extrai próximo componente
        int ci = 0;
        while (*p && *p != '/' && ci < VFS_NAME_MAX - 1)
            component[ci++] = *p++;
        component[ci] = 0;
        if (*p == '/') p++;

        if (!ci || kstrcmp(component, ".") == 0) continue;

        if (kstrcmp(component, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }

        VfsNode* next = vfs_find_child(cur, component);
        if (!next) return NULL;
        cur = next;
    }
    return cur;
}

VfsNode* vfs_mkdir(VfsNode* parent, const char* name) {
    if (!parent || !name || !name[0]) return NULL;
    if (kstrlen(name) >= VFS_NAME_MAX) return NULL; // nome longo demais
    if (parent->type != VFS_DIR) return NULL;
    if (vfs_find_child(parent, name)) return NULL; // já existe

    VfsNode* n = vfs_alloc_node(name, VFS_DIR);
    if (!n) return NULL;
    if (!vfs_add_child(parent, n)) return NULL;
    vfs_persist();
    return n;
}

VfsNode* vfs_touch(VfsNode* parent, const char* name) {
    if (!parent || !name || !name[0]) return NULL;
    if (kstrlen(name) >= VFS_NAME_MAX) return NULL; // nome longo demais
    if (parent->type != VFS_DIR) return NULL;

    // Se já existe e é arquivo, retorna ele
    VfsNode* existing = vfs_find_child(parent, name);
    if (existing) return (existing->type == VFS_FILE) ? existing : NULL;

    VfsNode* n = vfs_alloc_node(name, VFS_FILE);
    if (!n) return NULL;
    // Aloca buffer inicial de dados
    n->data = (char*)kzalloc(VFS_FILE_INITIAL);
    if (!n->data) return NULL;
    n->capacity = VFS_FILE_INITIAL;
    if (!vfs_add_child(parent, n)) return NULL;
    vfs_persist();
    return n;
}

// Garante que `file->data` tenha pelo menos `needed` bytes de capacidade,
// dobrando o tamanho até chegar lá (ou até o limite VFS_FILE_MAX).
bool vfs_ensure_capacity(VfsNode* file, uint32_t needed) {
    if (!file || file->type != VFS_FILE) return false;
    if (needed > VFS_FILE_MAX) needed = VFS_FILE_MAX;
    if (file->capacity >= needed) return true;

    uint32_t new_cap = file->capacity ? file->capacity : VFS_FILE_INITIAL;
    while (new_cap < needed && new_cap < VFS_FILE_MAX) new_cap *= 2;
    if (new_cap > VFS_FILE_MAX) new_cap = VFS_FILE_MAX;

    char* new_data = (char*)kzalloc(new_cap);
    if (!new_data) return false;

    if (file->data && file->size > 0)
        kmemcpy(new_data, file->data, file->size);
    if (file->data) kfree(file->data);

    file->data = new_data;
    file->capacity = new_cap;
    return true;
}

bool vfs_write(VfsNode* file, const char* data) {
    if (!file || file->type != VFS_FILE) return false;
    size_t len = kstrlen(data);
    if (len >= VFS_FILE_MAX) len = VFS_FILE_MAX - 1;
    if (!vfs_ensure_capacity(file, (uint32_t)(len + 1))) return false;
    kmemcpy(file->data, data, len);
    file->data[len] = 0;
    file->size = (uint32_t)len;
    return vfs_persist();
}

bool vfs_append(VfsNode* file, const char* data) {
    if (!file || file->type != VFS_FILE) return false;
    size_t cur_len = file->size;
    size_t add_len = kstrlen(data);
    size_t total = cur_len + add_len;
    if (total >= VFS_FILE_MAX) {
        add_len = VFS_FILE_MAX - 1 - cur_len;
        total = VFS_FILE_MAX - 1;
    }
    if (!vfs_ensure_capacity(file, (uint32_t)(total + 1))) return false;
    kmemcpy(file->data + cur_len, data, add_len);
    file->size = (uint32_t)(cur_len + add_len);
    file->data[file->size] = 0;
    return vfs_persist();
}

static bool vfs_remove_node(VfsNode* node) {
    if (!node || !node->parent) return false; // não pode remover root
    VfsNode* parent = node->parent;

    // Remove do array de filhos do pai
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            // Shift
            for (uint32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->children[--parent->child_count] = NULL;
            break;
        }
    }
    // Se é diretório com filhos, remove recursivamente
    if (node->type == VFS_DIR) {
        // Recursão simples — remove todos filhos primeiro
        while (node->child_count > 0)
            vfs_remove_node(node->children[0]);
    } else if (node->data) {
        kfree(node->data);
        node->data = NULL;
    }

    // Deixa o nó num estado "tombstone" consistente. O nó em si não
    // é liberado (limitação conhecida do VFS — ver comentário no topo
    // do arquivo), então ponteiros antigos para ele (fd_table de
    // processos com o arquivo aberto) continuam apontando para
    // memória válida. Antes, `data` virava NULL mas `size` e
    // `capacity` ficavam com os valores antigos — um sys_read num fd
    // ainda aberto calcularia `node->data + offset` com data == NULL
    // e leria memória baixa do kernel. Agora size/capacity zeram e o
    // flag `removed` permite às syscalls recusarem o acesso.
    node->size     = 0;
    node->capacity = 0;
    node->removed  = true;
    return true;
}

bool vfs_rm(VfsNode* node) {
    if (!vfs_remove_node(node)) return false;
    return vfs_persist();
}

bool vfs_sync(void) {
    return vfs_persist();
}

// ---- Renomear / mover / copiar ----------------------------------
bool vfs_name_valid(const char* name) {
    if (!name || !name[0]) return false;
    if (kstrlen(name) >= VFS_NAME_MAX) return false;
    if (kstrcmp(name, ".") == 0 || kstrcmp(name, "..") == 0) return false;
    for (const char* p = name; *p; p++)
        if (*p == '/') return false;
    return true;
}

// `anc` é `n` ou algum ancestral de `n`?
static bool vfs_is_ancestor_or_self(const VfsNode* anc, const VfsNode* n) {
    for (; n; n = n->parent)
        if (n == anc) return true;
    return false;
}

bool vfs_rename(VfsNode* node, VfsNode* new_parent, const char* new_name) {
    if (!node || node->removed || !node->parent || node == g_root) return false;
    if (!new_parent || new_parent->removed || new_parent->type != VFS_DIR) return false;
    if (!vfs_name_valid(new_name)) return false;
    if (vfs_is_ancestor_or_self(node, new_parent)) return false;   // pasta dentro de si mesma

    VfsNode* existing = vfs_find_child(new_parent, new_name);
    if (existing && existing != node) return false;                // não sobrescreve

    VfsNode* old_parent = node->parent;
    if (old_parent != new_parent) {
        if (new_parent->child_count >= VFS_MAX_CHILDREN) return false;
        for (uint32_t i = 0; i < old_parent->child_count; i++) {
            if (old_parent->children[i] == node) {
                for (uint32_t j = i; j + 1 < old_parent->child_count; j++)
                    old_parent->children[j] = old_parent->children[j + 1];
                old_parent->children[--old_parent->child_count] = NULL;
                break;
            }
        }
        new_parent->children[new_parent->child_count++] = node;
        node->parent = new_parent;
    }
    kmemset(node->name, 0, sizeof(node->name));
    kstrncpy(node->name, new_name, VFS_NAME_MAX - 1);
    return vfs_persist();
}

// Copia recursiva. `depth` limita a recursão (árvores absurdamente profundas).
static VfsNode* copy_tree(const VfsNode* src, VfsNode* dst_parent, const char* name, uint32_t depth) {
    if (depth > 32) return NULL;
    VfsNode* n;
    if (src->type == VFS_DIR) {
        n = vfs_mkdir(dst_parent, name);
        if (!n) return NULL;
        for (uint32_t i = 0; i < src->child_count; i++) {
            const VfsNode* ch = src->children[i];
            if (ch->removed) continue;
            if (!copy_tree(ch, n, ch->name, depth + 1)) return NULL;
        }
    } else {
        n = vfs_touch(dst_parent, name);
        if (!n) return NULL;
        if (src->size > 0) {
            if (!vfs_ensure_capacity(n, src->size + 1)) return NULL;
            kmemcpy(n->data, src->data, src->size);
            n->data[src->size] = 0;
            n->size = src->size;
        }
    }
    n->uid = src->uid;
    n->gid = src->gid;
    n->mode = src->mode;
    return n;
}

VfsNode* vfs_copy(const VfsNode* src, VfsNode* dst_parent, const char* dst_name) {
    if (!src || src->removed || !dst_parent || dst_parent->removed) return NULL;
    if (dst_parent->type != VFS_DIR || !vfs_name_valid(dst_name)) return NULL;
    if (vfs_find_child(dst_parent, dst_name)) return NULL;                 // não sobrescreve
    if (src->type == VFS_DIR && vfs_is_ancestor_or_self(src, dst_parent)) return NULL;

    vfs_batch_begin();
    VfsNode* n = copy_tree(src, dst_parent, dst_name, 0);
    if (!n) {                                                              // desfaz o parcial
        VfsNode* partial = vfs_find_child(dst_parent, dst_name);
        if (partial) vfs_remove_node(partial);
    }
    vfs_batch_end();
    return n;
}

void vfs_path_of(VfsNode* node, char* buf, size_t bufsz) {
    if (!node || !buf || bufsz == 0) return;
    if (node == g_root) { buf[0] = '/'; buf[1] = 0; return; }

    // Constrói caminho de trás pra frente
    char tmp[512];
    tmp[0] = 0;
    VfsNode* cur = node;
    while (cur && cur != g_root) {
        char seg[VFS_NAME_MAX + 2];
        seg[0] = '/';
        kstrncpy(seg + 1, cur->name, sizeof(seg) - 1);
        seg[sizeof(seg) - 1] = '\0';
        // Prepend seg to tmp — com limite em ambas as cópias: uma
        // hierarquia de diretórios suficientemente profunda faria o
        // caminho acumulado passar de 512 bytes, estourando `tmp` (e
        // `old`) sem nenhum aviso. Com kstrncat o caminho é truncado
        // em vez de corromper a pilha; truncar é o comportamento
        // correto aqui, já que quem chama passa um `bufsz` próprio e
        // já espera um resultado possivelmente cortado.
        char old[512];
        kstrncpy(old, tmp, sizeof(old));
        old[sizeof(old) - 1] = '\0';
        kstrncpy(tmp, seg, sizeof(tmp));
        tmp[sizeof(tmp) - 1] = '\0';
        kstrncat(tmp, old, sizeof(tmp));
        cur = cur->parent;
    }
    if (!tmp[0]) { tmp[0] = '/'; tmp[1] = 0; }

    size_t len = kstrlen(tmp);
    if (len >= bufsz) len = bufsz - 1;
    kmemcpy(buf, tmp, len);
    buf[len] = 0;
}
