#pragma once
#include "../kernel/types.h"

// ---- Limites ------------------------------------------------
#define VFS_NAME_MAX    64
#define VFS_FILE_MAX    256      // bytes máx por arquivo
#define VFS_MAX_CHILDREN 32      // máx filhos por diretório

// ---- Tipos de nó --------------------------------------------
typedef enum {
    VFS_DIR  = 0,
    VFS_FILE = 1,
} VfsType;

// ---- Nó do FS -----------------------------------------------
typedef struct VfsNode {
    char           name[VFS_NAME_MAX];
    VfsType        type;
    uint32_t       size;                    // apenas para FILE
    char*          data;                    // conteúdo (FILE) ou NULL (DIR)
    struct VfsNode* parent;
    struct VfsNode* children[VFS_MAX_CHILDREN];
    uint32_t        child_count;
} VfsNode;

// ---- API pública --------------------------------------------
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

void      vfs_path_of(VfsNode* node, char* buf, size_t bufsz);
