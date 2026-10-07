#pragma once
#include "../kernel/types.h"

// ---- Limites ------------------------------------------------
#define VFS_NAME_MAX     64
#define VFS_FILE_INITIAL 256       // capacidade inicial alocada por arquivo (bytes)
#define VFS_FILE_MAX     (1024 * 1024)   // 1 MB máx por arquivo (cresce dinamicamente)
#define VFS_MAX_CHILDREN 128       // máx filhos por diretório

// ---- Permissões (estilo Unix) -------------------------------
// mode = 0rwxrwxrwx (dono/grupo/outros) + bit "sticky" 01000.
// Em diretório: r = listar, w = criar/remover entradas, x = atravessar.
// Em arquivo: r = ler, w = escrever. (O bit x de arquivo não é exigido
// para executar um .hexec — basta poder LER o arquivo.)
#define VFS_MODE_DIR_DEFAULT   0755
#define VFS_MODE_FILE_DEFAULT  0644
#define VFS_MODE_STICKY        01000   // em diretório: só o dono do arquivo/diretório remove
#define VFS_MODE_MASK          01777

// ---- Tipos de nó --------------------------------------------
typedef enum {
    VFS_DIR  = 0,
    VFS_FILE = 1,
} VfsType;

// ---- Nó do FS -----------------------------------------------
typedef struct VfsNode {
    char           name[VFS_NAME_MAX];
    VfsType        type;
    uint32_t       size;                    // bytes usados (apenas para FILE)
    uint32_t       capacity;                // bytes alocados em `data` (apenas para FILE)
    char*          data;                    // conteúdo (FILE) ou NULL (DIR)
    uint32_t       fs_inode;                // inode persistente, quando montado
    uint32_t       uid;                     // dono (0 = root). Persistido no inode.
    uint32_t       gid;                     // grupo (por ora = uid do dono)
    uint16_t       mode;                    // permissões, ver VFS_MODE_*
    struct VfsNode* parent;
    struct VfsNode* children[VFS_MAX_CHILDREN];
    uint32_t        child_count;
    bool            removed;                // true depois de vfs_rm: o nó em si NÃO é liberado
                                            // (pode haver ponteiros pendurados — ex.: fd_table
                                            // de um processo que tinha o arquivo aberto), mas
                                            // `data` já foi liberado e `size`/`capacity` zerados.
                                            // Quem guarda VfsNode* por tempo indefinido deve
                                            // checar este campo antes de usar `data`.
} VfsNode;

// ---- API pública --------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

void      vfs_init(void);

VfsNode*  vfs_root(void);
VfsNode*  vfs_cwd(void);
void      vfs_set_cwd(VfsNode* node);

// Navegação
VfsNode*  vfs_find_child(VfsNode* dir, const char* name);
VfsNode*  vfs_resolve(VfsNode* base, const char* path);  

// Operações
VfsNode*  vfs_mkdir(VfsNode* parent, const char* name);
VfsNode*  vfs_touch(VfsNode* parent, const char* name);
bool      vfs_write(VfsNode* file, const char* data);
bool      vfs_append(VfsNode* file, const char* data);
bool      vfs_rm(VfsNode* node);
bool      vfs_sync(void);

// Dono e permissões — SEM checagem (quem chama é o kernel; para checar
// em nome de um usuário use fs/perm.h: vfs_chmod_as / vfs_chown_as).
// Não grava no disco: chame vfs_sync() (ou feche um lote) depois.
void      vfs_set_owner(VfsNode* node, uint32_t uid, uint32_t gid, uint16_t mode);

// Lote de gravações: enquanto houver um lote aberto, vfs_mkdir/touch/
// write/append/rm NÃO regravam o FS no disco — marcam "sujo" e a gravação
// acontece UMA vez em vfs_batch_end() (quando o último lote fecha).
// Evita regravar o filesystem inteiro 4-5 vezes ao criar um usuário.
// Lotes podem aninhar. Retorna false se a gravação final falhou.
void      vfs_batch_begin(void);
bool      vfs_batch_end(void);

// Cresce o buffer de um arquivo se necessário (usado por write/append e pelo editor)
bool      vfs_ensure_capacity(VfsNode* file, uint32_t needed);

void      vfs_path_of(VfsNode* node, char* buf, size_t bufsz);

#ifdef __cplusplus
}
#endif
