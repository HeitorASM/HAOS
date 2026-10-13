#include "applaunch.h"
#include "../../fs/vfs.h"
#include "../../fs/perm.h"
#include "../../kernel/process.h"
#include "../../kernel/user.h"
#include "../../kernel/memory.h"

bool app_launch(const char* hexec_path, const char* arg1, const char* arg2) {
    if (!hexec_path || !hexec_path[0]) return false;

    Cred cred = session_cred();
    VfsNode* node = nullptr;
    if (vfs_resolve_as(&cred, vfs_root(), hexec_path, &node) != PERM_OK || !node) return false;
    if (node->type != VFS_FILE || !perm_allows(node, &cred, PERM_R)) return false;

    // argv_str: "programa\0arg1\0arg2\0" (argv[0] = caminho do programa).
    char argv[1400];
    uint64_t len = 0;
    size_t n0 = kstrlen(hexec_path);
    if (n0 > 255) n0 = 255;
    kmemcpy(argv, hexec_path, n0);
    argv[n0] = '\0';
    len = n0 + 1;
    if (arg1 && arg1[0]) {
        size_t n1 = kstrlen(arg1);
        if (n1 > 512) n1 = 512;
        kmemcpy(argv + len, arg1, n1);
        argv[len + n1] = '\0';
        len += n1 + 1;
    }

    if (arg2 && arg2[0]) {
        size_t n2 = kstrlen(arg2);
        if (n2 > 512) n2 = 512;
        kmemcpy(argv + len, arg2, n2);
        argv[len + n2] = '\0';
        len += n2 + 1;
    }

    task_t* t = process_create(hexec_path, node, argv, len);
    return t != nullptr;      // stdout_owner fica nulo: a saída de texto é descartada
}
