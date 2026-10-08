#pragma once
#include "types.h"

// ============================================================
//  kernel/acpi.h — ACPI mínimo: desligar (S5) e reiniciar.
//
//  Fluxo: RSDP (tag do Multiboot2 ou busca na BIOS) -> RSDT/XSDT -> FADT
//  -> liga o modo ACPI (SCI_EN) -> lê \_S5 do DSDT -> escreve em PM1x_CNT.
//  Sem interpretador AML: só o padrão fixo do objeto \_S5.
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

// Chame depois de paging_init(). Seguro mesmo sem ACPI (só fica "indisponível").
void acpi_init(uint32_t mb2_info);

bool acpi_available(void);      // FADT achada
bool acpi_can_poweroff(void);   // FADT + \_S5 lidos

// Não retornam. Quem chama já deve ter gravado o disco (veja power.h).
void acpi_poweroff(void) __attribute__((noreturn));
void acpi_reboot(void)   __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
