#pragma once
#include "block.h"

// Leitura (somente leitura!) de tabelas de partição MBR e GPT.
// O HAOS procura uma partição do tipo HAOS:
//   MBR: tipo 0xDA ("non-FS data")
//   GPT: GUID 48414F53-4653-4841-4F53-465300000001
#define PART_MBR_TYPE_HAOS 0xDAu

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PART_NONE = 0,   // nenhuma tabela reconhecida
    PART_MBR,        // MBR com ao menos uma partição
    PART_GPT         // GPT (ou MBR protetivo + cabeçalho GPT válido)
} PartScheme;

typedef struct {
    uint64_t start;  // primeiro setor da partição HAOS
    uint64_t count;  // tamanho em setores
} PartRange;

// Devolve o esquema encontrado. *has_haos = true se há partição HAOS (e então
// *haos é preenchido, com os limites já validados contra o tamanho do disco).
PartScheme partition_scan(BlockDevice* dev, PartRange* haos, bool* has_haos);

#ifdef __cplusplus
}
#endif
