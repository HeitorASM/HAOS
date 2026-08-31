#pragma once
#include "../kernel/types.h"

// ---- Limites ------------------------------------------------
#define VFS_NAME_MAX     64
#define VFS_FILE_INITIAL 256       // capacidade inicial alocada por arquivo (bytes)
#define VFS_FILE_MAX     (1024 * 1024)   // 1 MB máx por arquivo (cresce dinamicamente)
#define VFS_MAX_CHILDREN 128       // máx filhos por diretório

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
    struct VfsNode* parent;
    struct VfsNode* children[VFS_MAX_CHILDREN];
    uint32_t        child_count;
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

// Cresce o buffer de um arquivo se necessário (usado por write/append e pelo editor)
bool      vfs_ensure_capacity(VfsNode* file, uint32_t needed);

void      vfs_path_of(VfsNode* node, char* buf, size_t bufsz);

#ifdef __cplusplus
}
#endif
