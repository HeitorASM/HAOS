#include "perm.h"
#include "../kernel/memory.h"

// Bits de permissão (rwx) que valem para `cred` sobre `node`: dono, grupo
// ou outros — a primeira classe que combina, como no Unix (se você é o
// dono, vale SÓ a classe "dono", mesmo que "outros" tenha mais permissão).
static unsigned perm_class_bits(const VfsNode* node, const Cred* cred) {
    unsigned m = node->mode;
    if (cred->uid == node->uid) return (m >> 6) & 7u;
    if (cred->gid == node->gid) return (m >> 3) & 7u;
    return m & 7u;
}

bool perm_allows(const VfsNode* node, const Cred* cred, int want) {
    if (!node || !cred || node->removed) return false;
    if (cred->bypass) return true;
    return (perm_class_bits(node, cred) & (unsigned)want) == (unsigned)want;
}

int vfs_resolve_as(const Cred* cred, VfsNode* base, const char* path, VfsNode** out) {
    if (!cred || !out) return PERM_EINVAL;
    *out = NULL;

    if (!path || !path[0]) {
        VfsNode* b = base ? base : vfs_cwd();
        if (!b) return PERM_ENOENT;
        *out = b;
        return PERM_OK;
    }

    VfsNode* cur;
    const char* p = path;
    if (p[0] == '/') {
        cur = vfs_root();
        p++;
    } else {
        cur = base ? base : vfs_cwd();
    }
    if (!cur) return PERM_ENOENT;

    char component[VFS_NAME_MAX];
    while (*p) {
        int ci = 0;
        while (*p && *p != '/' && ci < VFS_NAME_MAX - 1)
            component[ci++] = *p++;
        component[ci] = 0;
        if (*p == '/') p++;

        if (!ci || kstrcmp(component, ".") == 0) continue;

        // Para olhar QUALQUER entrada de `cur` (inclusive "..") é preciso
        // ser diretório e ter permissão de busca (X) nele.
        if (cur->type != VFS_DIR) return PERM_ENOENT;
        if (!perm_allows(cur, cred, PERM_X)) return PERM_EACCES;

        if (kstrcmp(component, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }

        VfsNode* next = vfs_find_child(cur, component);
        if (!next) return PERM_ENOENT;
        cur = next;
    }
    *out = cur;
    return PERM_OK;
}

static VfsNode* create_as(const Cred* cred, VfsNode* parent, const char* name,
                          int* err, bool is_dir) {
    if (err) *err = PERM_OK;
    if (!cred || !parent || parent->type != VFS_DIR || parent->removed) {
        if (err) *err = PERM_ENOENT;
        return NULL;
    }

    if (!is_dir) {
        // vfs_touch devolve o arquivo já existente: não exige nada aqui.
        VfsNode* existing = vfs_find_child(parent, name);
        if (existing) {
            if (existing->type == VFS_FILE) return existing;
            if (err) *err = PERM_EINVAL;     // há um diretório com esse nome
            return NULL;
        }
    }

    if (!perm_allows(parent, cred, PERM_W | PERM_X)) {
        if (err) *err = PERM_EACCES;
        return NULL;
    }

    // Lote: criar o nó e acertar o dono numa gravação só — sem isso o disco
    // ficaria, até a próxima gravação, com o nó pertencendo a root.
    vfs_batch_begin();
    VfsNode* n = is_dir ? vfs_mkdir(parent, name) : vfs_touch(parent, name);
    if (n) {
        vfs_set_owner(n, cred->uid, cred->gid,
                      is_dir ? VFS_MODE_DIR_DEFAULT : VFS_MODE_FILE_DEFAULT);
    }
    vfs_batch_end();

    if (!n && err) *err = PERM_EINVAL;
    return n;
}

VfsNode* vfs_mkdir_as(const Cred* cred, VfsNode* parent, const char* name, int* err) {
    return create_as(cred, parent, name, err, true);
}

VfsNode* vfs_touch_as(const Cred* cred, VfsNode* parent, const char* name, int* err) {
    return create_as(cred, parent, name, err, false);
}

// `dir` pode perder a entrada `child` para `cred`? (W+X no diretório + sticky)
static bool may_unlink_entry(const Cred* cred, const VfsNode* dir, const VfsNode* child) {
    if (!perm_allows(dir, cred, PERM_W | PERM_X)) return false;
    if ((dir->mode & VFS_MODE_STICKY) && !cred->bypass &&
        cred->uid != child->uid && cred->uid != dir->uid)
        return false;
    return true;
}

// vfs_rm remove diretórios RECURSIVAMENTE — então, antes, confere que cada
// entrada de cada subdiretório também poderia ser removida por `cred`.
// Sem isto, bastaria ter W no diretório pai para apagar o conteúdo de
// outros usuários que estivesse dentro dele.
static bool tree_removable(const Cred* cred, const VfsNode* dir) {
    if (dir->type != VFS_DIR) return true;
    for (uint32_t i = 0; i < dir->child_count; i++) {
        const VfsNode* ch = dir->children[i];
        if (!may_unlink_entry(cred, dir, ch)) return false;
        if (!tree_removable(cred, ch)) return false;
    }
    return true;
}

int vfs_remove_as(const Cred* cred, VfsNode* node) {
    if (!cred || !node || node->removed) return PERM_ENOENT;
    VfsNode* parent = node->parent;
    if (!parent) return PERM_EINVAL;                       // raiz

    if (!may_unlink_entry(cred, parent, node)) return PERM_EACCES;
    if (!tree_removable(cred, node)) return PERM_EACCES;

    return vfs_rm(node) ? PERM_OK : PERM_EINVAL;
}

int vfs_chmod_as(const Cred* cred, VfsNode* node, uint16_t mode) {
    if (!cred || !node || node->removed) return PERM_ENOENT;
    if (!cred->bypass && cred->uid != node->uid) return PERM_EACCES;
    vfs_set_owner(node, node->uid, node->gid, (uint16_t)(mode & VFS_MODE_MASK));
    vfs_sync();
    return PERM_OK;
}

int vfs_chown_as(const Cred* cred, VfsNode* node, uint32_t uid, uint32_t gid) {
    if (!cred || !node || node->removed) return PERM_ENOENT;
    if (!cred->bypass) return PERM_EACCES;                 // só root/admin dá arquivos
    vfs_set_owner(node, uid, gid, node->mode);
    vfs_sync();
    return PERM_OK;
}

void vfs_chown_tree(VfsNode* node, uint32_t uid, uint32_t gid) {
    if (!node || node->removed) return;
    vfs_set_owner(node, uid, gid, node->mode);
    if (node->type == VFS_DIR) {
        for (uint32_t i = 0; i < node->child_count; i++)
            vfs_chown_tree(node->children[i], uid, gid);
    }
}

void perm_format(const VfsNode* node, char out[11]) {
    static const char sym[3] = { 'r', 'w', 'x' };
    out[0] = (node->type == VFS_DIR) ? 'd' : '-';
    for (int i = 0; i < 9; i++) {
        bool set = (node->mode >> (8 - i)) & 1u;
        out[1 + i] = set ? sym[i % 3] : '-';
    }
    // Sticky: o 'x' de "outros" vira 't' (ou 'T' se não havia x).
    if (node->mode & VFS_MODE_STICKY)
        out[9] = (out[9] == 'x') ? 't' : 'T';
    out[10] = '\0';
}
