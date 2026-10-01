#include "vfs.h"
#include "../kernel/memory.h"
#include "haosfs.h"
#include "../drivers/ata_pio.h"
#include "../kernel/generated/hello_hexec_data.h"
#include "../kernel/generated/hello_data_hexec_data.h"
#include "../kernel/generated/hello_full_hexec_data.h"
#include "../kernel/generated/calc_hexec_data.h"
#include "../kernel/generated/notes_hexec_data.h"
#include "../kernel/generated/launcher_hexec_data.h"
// Sistema de arquivos virtual que roda na RAM do OS.
// Arquivos crescem dinamicamente (dobra de capacidade) até VFS_FILE_MAX,
// e diretórios suportam até VFS_MAX_CHILDREN itens.
// Se deletar tem uma "chance" de vazar memória (limitação conhecida, ok para um SO educacional).


// ---- Estado global ------------------------------------------
static VfsNode* g_root = NULL;
static VfsNode* g_cwd  = NULL;
static bool g_persistent = false;

static bool vfs_persist(void) {
    if (!g_persistent) return true;
    return haosfs_save(ata_pio_device(), g_root);
}

// ---- Helpers internos ---------------------------------------

static VfsNode* vfs_alloc_node(const char* name, VfsType type) {
    VfsNode* n = (VfsNode*)kzalloc(sizeof(VfsNode));
    if (!n) return NULL;
    kstrcpy(n->name, name);
    n->type = type;
    n->size = 0;
    n->capacity = 0;
    n->data = NULL;
    n->fs_inode = 0;
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

static void vfs_create_defaults(void) {
    VfsNode* bin  = vfs_mkdir(g_root, "bin");
    VfsNode* home = vfs_mkdir(g_root, "home");
    VfsNode* etc  = vfs_mkdir(g_root, "etc");
    VfsNode* tmp  = vfs_mkdir(g_root, "tmp");

    (void)bin; (void)tmp;

    // /bin/hello.hexec — executável de exemplo, escrito em C de
    // verdade (ver examples/hello_c/main.c) usando a libc mínima do
    // HAOS (libc/), compilado e empacotado via libc/mkhexec.py.
    // Este header (kernel/generated/hello_hexec_data.h) é um
    // artefato de BUILD gerado a partir daquele fonte — nunca edite
    // este .h manualmente; edite examples/hello_c/main.c e regenere
    // (ver libc/README.md para o passo a passo completo).
    // Escrito bytes-a-bytes via vfs_ensure_capacity+kmemcpy (não
    // vfs_write, que corta no primeiro '\0' — inaceitável para
    // código de máquina binário, que naturalmente contém bytes zero
    // no meio). Rode com "run hello.hexec" no terminal.
    {
        VfsNode* hello = vfs_touch(bin, "hello.hexec");
        if (hello && vfs_ensure_capacity(hello, HELLO_HEXEC_SIZE)) {
            kmemcpy(hello->data, hello_hexec_data, HELLO_HEXEC_SIZE);
            hello->size = HELLO_HEXEC_SIZE;
        }
    }

    // /bin/hello_data.hexec — segundo exemplo, validando HEXEC v2 de
    // ponta a ponta: usa uma variável .data (inicializada, valor != 0)
    // e uma .bss (não inicializada) — ambas mapeadas RW+NX pelo
    // loader, em páginas SEPARADAS de .text (RX). Ver
    // examples/hello_data/main.c. Rode com "run hello_data.hexec".
    {
        VfsNode* hello_data = vfs_touch(bin, "hello_data.hexec");
        if (hello_data && vfs_ensure_capacity(hello_data, HELLO_DATA_HEXEC_SIZE)) {
            kmemcpy(hello_data->data, hello_data_hexec_data, HELLO_DATA_HEXEC_SIZE);
            hello_data->size = HELLO_DATA_HEXEC_SIZE;
        }
    }

    // /bin/hello_full.hexec — terceiro exemplo, testando as syscalls
    // novas de uma vez: argc/argv (passe argumentos com
    // "run hello_full.hexec foo bar"), malloc/free (sys_sbrk), e
    // open/read/close (lê o próprio /bin/hello.hexec e confere o
    // magic HXEC). Ver examples/hello_full/main.c.
    {
        VfsNode* hello_full = vfs_touch(bin, "hello_full.hexec");
        if (hello_full && vfs_ensure_capacity(hello_full, HELLO_FULL_HEXEC_SIZE)) {
            kmemcpy(hello_full->data, hello_full_hexec_data, HELLO_FULL_HEXEC_SIZE);
            hello_full->size = HELLO_FULL_HEXEC_SIZE;
        }
    }

    // /bin/calc.hexec — calculadora interativa de linha de comando,
    // testando stdin de verdade: le expressões "numero op numero"
    // via read(0, ...) (syscall SYS_READ com fd=0, bloqueante — ver
    // sys_read_stdin em kernel/syscall.c) e imprime o resultado em
    // loop, até "sair"/"exit". Usa aritmética fixed-point (inteiros
    // em centésimos) em vez de float/double, já que processos de
    // usuário rodam sem FPU/SSE (ver comentário em
    // examples/calc/main.c). Rode com "run calc.hexec".
    {
        VfsNode* calc = vfs_touch(bin, "calc.hexec");
        if (calc && vfs_ensure_capacity(calc, CALC_HEXEC_SIZE)) {
            kmemcpy(calc->data, calc_hexec_data, CALC_HEXEC_SIZE);
            calc->size = CALC_HEXEC_SIZE;
        }
    }

    // /bin/notes.hexec — testa scanf() + escrita de arquivo via
    // open2(O_WRONLY|O_CREAT|O_TRUNC) e write_fd() (SYS_WRITE_FD).
    // Ver examples/notes/main.c. Rode com "run notes.hexec".
    {
        VfsNode* notes = vfs_touch(bin, "notes.hexec");
        if (notes && vfs_ensure_capacity(notes, NOTES_HEXEC_SIZE)) {
            kmemcpy(notes->data, notes_hexec_data, NOTES_HEXEC_SIZE);
            notes->size = NOTES_HEXEC_SIZE;
        }
    }

    // /bin/launcher.hexec — demonstra spawn() (SYS_SPAWN): inicia dois
    // processos filho (/bin/hello.hexec) e termina. Ver
    // examples/launcher/main.c. Rode com "run launcher.hexec".
    {
        VfsNode* launcher = vfs_touch(bin, "launcher.hexec");
        if (launcher && vfs_ensure_capacity(launcher, LAUNCHER_HEXEC_SIZE)) {
            kmemcpy(launcher->data, launcher_hexec_data, LAUNCHER_HEXEC_SIZE);
            launcher->size = LAUNCHER_HEXEC_SIZE;
        }
    }

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

void vfs_init(void) {
    g_persistent = false;
    g_root = vfs_alloc_node("/", VFS_DIR);
    g_cwd  = g_root;

    BlockDevice* device = ata_pio_device();
    HaosFsProbe probe = haosfs_probe(device);
    if (probe == HAOSFS_VALID && haosfs_mount(device, g_root)) {
        g_persistent = true;
        VfsNode* user = vfs_resolve(g_root, "/home/user");
        if (user && user->type == VFS_DIR) g_cwd = user;
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
