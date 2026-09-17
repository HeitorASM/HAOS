#include "file_manager_fs.h"
#include "../../kernel/memory.h"

VfsNode* file_manager_child(VfsNode* dir, uint32_t index) {
    if (!dir || dir->type != VFS_DIR || index >= dir->child_count)
        return NULL;
    return dir->children[index];
}

VfsNode* file_manager_parent(VfsNode* dir) {
    if (!dir || !dir->parent)
        return dir;
    return dir->parent;
}

bool file_manager_is_text(const VfsNode* node) {
    if (!node || node->type != VFS_FILE)
        return false;
    if (node->size > 0 && !node->data)
        return false;
    for (uint32_t i = 0; i < node->size; i++) {
        if (node->data[i] == '\0')
            return false;
    }
    return true;
}

bool file_manager_is_executable(const VfsNode* node) {
    if (!node || node->type != VFS_FILE) return false;

    // Compara o sufixo do nome com ".hexec" (case-sensitive — o
    // HAOS não normaliza maiúsculas/minúsculas em nomes de arquivo
    // em nenhum outro lugar do VFS, então manter a mesma
    // convenção aqui evita uma inconsistência sutil).
    size_t name_len = kstrlen(node->name);
    const char* suffix = ".hexec";
    size_t suffix_len = kstrlen(suffix);
    if (name_len < suffix_len) return false;

    const char* tail = node->name + (name_len - suffix_len);
    for (size_t i = 0; i < suffix_len; i++) {
        if (tail[i] != suffix[i]) return false;
    }
    return true;
}

void file_manager_path(const VfsNode* node, char* buffer, size_t buffer_size) {
    vfs_path_of((VfsNode*)node, buffer, buffer_size);
}
