#pragma once
#include "../../fs/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

VfsNode* file_manager_child(VfsNode* dir, uint32_t index);
VfsNode* file_manager_parent(VfsNode* dir);
bool file_manager_is_text(const VfsNode* node);

// Detecta se `node` é um executável suportado pelo sistema, pelo
// nome do arquivo (extensão), não pelo conteúdo — assim a checagem
// é barata e não depende de já termos o header HEXEC parseado.
// Hoje só reconhece ".hexec" (ver kernel/hexec.h); outros formatos
// de executável futuros (ex: ELF) devem ser adicionados aqui.
bool file_manager_is_executable(const VfsNode* node);

void file_manager_path(const VfsNode* node, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
