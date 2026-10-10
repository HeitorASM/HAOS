#pragma once
#include "vfs.h"

// ============================================================
//  fs/perm.h — Controle de acesso do VFS (modelo Unix simplificado)
//
//  Cada VfsNode tem dono (uid), grupo (gid) e mode (rwxrwxrwx + sticky).
//  Quem quer acessar um nó é descrito por um `Cred`:
//
//    - processo Ring 3 (syscalls): uid/gid da task; bypass só se uid == 0
//    - código de kernel agindo em nome do usuário logado (terminal,
//      editor, explorador): session_cred() em kernel/user.h — administradores
//      têm bypass (equivalente a "root" na GUI/terminal)
//    - código de kernel "de sistema" (carregar tema, idioma, wallpaper,
//      montar o disco): chama a API SEM sufixo _as de fs/vfs.h, que não
//      checa nada. Só o KERNEL faz isso; um programa de usuário só chega
//      ao VFS por syscalls, que sempre usam estas funções _as.
//
//  Regras (como no Unix):
//    - atravessar um diretório (resolver um caminho que passa por ele)
//      exige X nele — por isso um /home/ana com modo 0700 esconde TUDO
//      que está dentro, mesmo arquivos com modo 0644;
//    - listar exige R no diretório; criar/remover entrada exige W+X nele;
//    - ler arquivo exige R; escrever exige W; rodar um .hexec exige R;
//    - diretório com bit sticky (01000, ex. /tmp): só o dono do arquivo,
//      o dono do diretório ou o bypass remove entradas;
//    - chmod: dono ou bypass; chown: somente bypass.
//
//  Grupos: por enquanto cada usuário tem um grupo próprio (gid == uid),
//  sem grupos suplementares.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t bypass;   // != 0: ignora as permissões (root / administrador)
} Cred;

#define PERM_R 4
#define PERM_W 2
#define PERM_X 1

#define PERM_OK       0
#define PERM_ENOENT  (-1)   // não existe / não é diretório no meio do caminho
#define PERM_EACCES  (-2)   // permissão negada
#define PERM_EINVAL  (-3)   // nome inválido, já existe, VFS cheio...

// `want` é uma combinação de PERM_R/W/X. Nó removido => false.
bool perm_allows(const VfsNode* node, const Cred* cred, int want);

// Como vfs_resolve(), mas exige X em cada diretório atravessado.
// `base` = diretório de partida para caminhos relativos (NULL => cwd global).
int  vfs_resolve_as(const Cred* cred, VfsNode* base, const char* path, VfsNode** out);

// Criação em nome de `cred` (exige W+X em `parent`; o novo nó pertence a
// cred->uid/gid). vfs_touch_as devolve o arquivo existente, sem exigir nada,
// se já houver um — quem vai ler/escrever deve checar perm_allows depois.
// Em falha devolve NULL e preenche *err (pode ser NULL) com PERM_E*.
VfsNode* vfs_mkdir_as(const Cred* cred, VfsNode* parent, const char* name, int* err);
VfsNode* vfs_touch_as(const Cred* cred, VfsNode* parent, const char* name, int* err);

// Remove `node` (arquivo ou diretório) se `cred` pode alterar o diretório pai
// (W+X) e a regra sticky permitir. Não remove a raiz.
int  vfs_remove_as(const Cred* cred, VfsNode* node);

// Move/renomeia `node` para (new_parent, new_name). Exige W+X no diretório de
// ORIGEM (com a regra sticky) e no de DESTINO. Não sobrescreve nada.
int  vfs_rename_as(const Cred* cred, VfsNode* node, VfsNode* new_parent, const char* new_name);

// Copia `src` (arquivo ou pasta) para (dst_parent, dst_name). Exige R em todo
// arquivo copiado, R+X em toda pasta copiada e W+X no destino; a cópia
// (toda a árvore) passa a pertencer a `cred`. Mantém os bits de modo.
// Em sucesso, *out (se não NULL) recebe a cópia.
int  vfs_copy_as(const Cred* cred, VfsNode* src, VfsNode* dst_parent,
                 const char* dst_name, VfsNode** out);

// Separa `path` em "diretório pai (resolvido, exige X no caminho)" + "último
// nome" (copiado em name_out, que precisa ter VFS_NAME_MAX bytes). Aceita '/'
// no final. PERM_EINVAL se o nome for inválido ("", ".", "..").
int  vfs_resolve_parent_as(const Cred* cred, VfsNode* base, const char* path,
                           VfsNode** parent, char* name_out);

// chmod/chown com checagem + gravação no disco.
int  vfs_chmod_as(const Cred* cred, VfsNode* node, uint16_t mode);
int  vfs_chown_as(const Cred* cred, VfsNode* node, uint32_t uid, uint32_t gid);

// Troca dono/grupo de `node` e de TUDO abaixo dele, sem checagem e sem
// gravar (uso interno do kernel: migração de contas, remoção de usuário).
void vfs_chown_tree(VfsNode* node, uint32_t uid, uint32_t gid);

// Formato longo estilo "ls -l": "drwxr-xr-x" / "-rw-r--r--" (com sticky:
// "drwxrwxrwt"). `out` precisa ter 11 bytes (10 caracteres + '\0').
void perm_format(const VfsNode* node, char out[11]);

#ifdef __cplusplus
}
#endif
