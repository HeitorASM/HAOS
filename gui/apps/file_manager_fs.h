#pragma once
#include "../../fs/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

VfsNode* file_manager_child(VfsNode* dir, uint32_t index);
VfsNode* file_manager_parent(VfsNode* dir);
bool file_manager_is_text(const VfsNode* node);
void file_manager_path(const VfsNode* node, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
