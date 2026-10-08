#pragma once
#include "block.h"

// Camada que ESCOLHE onde o HAOSFS vai morar. Procura discos IDE/ATA e
// SATA/AHCI e aplica regras de segurança para nunca tocar em dados alheios:
//
//   1. disco com partição HAOS (MBR 0xDA / GPT GUID HAOS) -> usa SÓ a partição
//   2. disco com HAOSFS válido no disco inteiro (formato antigo) -> usa
//   3. disco COMPLETAMENTE em branco (setor 0 zerado, sem tabela) -> usa (formata)
//   4. qualquer outro disco (Windows, Linux, tabela sem partição HAOS...) ->
//      NUNCA é tocado; o HAOS roda só em RAM
#ifdef __cplusplus
extern "C" {
#endif

void         storage_init(void);          // chame no lugar de ata_pio_init()
BlockDevice* storage_device(void);        // NULL = sem armazenamento persistente
const char*  storage_description(void);   // ex.: "SATA/AHCI #0, particao HAOS (GPT)"

#ifdef __cplusplus
}
#endif
