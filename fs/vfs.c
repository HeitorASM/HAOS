#include "vfs.h"
#include "../kernel/memory.h"
// Sistema de arquivos virtual que roda na RAM do OS.
// Arquivos crescem dinamicamente (dobra de capacidade) até VFS_FILE_MAX,
// e diretórios suportam até VFS_MAX_CHILDREN itens.
// Se deletar tem uma "chance" de vazar memória (limitação conhecida, ok para um SO educacional).


// ---- Estado global ------------------------------------------
static VfsNode* g_root = NULL;
static VfsNode* g_cwd  = NULL;

// ---- Helpers internos ---------------------------------------

static VfsNode* vfs_alloc_node(const char* name, VfsType type) {
    VfsNode* n = (VfsNode*)kzalloc(sizeof(VfsNode));
    if (!n) return NULL;
    kstrcpy(n->name, name);
    n->type = type;
    n->size = 0;
    n->capacity = 0;
    n->data = NULL;
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

void vfs_init(void) {
    g_root = vfs_alloc_node("/", VFS_DIR);
    g_cwd  = g_root;

    // Estrutura inicial do FS
    VfsNode* bin  = vfs_mkdir(g_root, "bin");
    VfsNode* home = vfs_mkdir(g_root, "home");
    VfsNode* etc  = vfs_mkdir(g_root, "etc");
    VfsNode* tmp  = vfs_mkdir(g_root, "tmp");

    (void)bin; (void)tmp;

    // /home/user
    VfsNode* user = vfs_mkdir(home, "user");

    // /etc/haos.conf
    VfsNode* conf = vfs_touch(etc, "haos.conf");
    vfs_write(conf, "# HAOS configuracao\nversion=1.1\ntheme=dark\n");

    // /home/user/README.txt
    VfsNode* readme = vfs_touch(user, "README.txt");
    vfs_write(readme, "Bem-vindo ao HAOS!\nUse 'help' para ver os comandos.\n");

    // CWD inicial: /home/user
    g_cwd = user;
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
    if (parent->type != VFS_DIR) return NULL;
    if (vfs_find_child(parent, name)) return NULL; // já existe

    VfsNode* n = vfs_alloc_node(name, VFS_DIR);
    if (!n) return NULL;
    if (!vfs_add_child(parent, n)) return NULL;
    return n;
}

VfsNode* vfs_touch(VfsNode* parent, const char* name) {
    if (!parent || !name || !name[0]) return NULL;
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
    return true;
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
    return true;
}

bool vfs_rm(VfsNode* node) {
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
            vfs_rm(node->children[0]);
    } else if (node->data) {
        kfree(node->data);
        node->data = NULL;
    }
    return true;
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
        kstrcpy(seg + 1, cur->name);
        // Prepend seg to tmp
        char old[512];
        kstrcpy(old, tmp);
        kstrcpy(tmp, seg);
        kstrcat(tmp, old);
        cur = cur->parent;
    }
    if (!tmp[0]) { tmp[0] = '/'; tmp[1] = 0; }

    size_t len = kstrlen(tmp);
    if (len >= bufsz) len = bufsz - 1;
    kmemcpy(buf, tmp, len);
    buf[len] = 0;
}
