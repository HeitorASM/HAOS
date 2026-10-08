#pragma once
// Desligar/reiniciar com segurança: grava o disco ANTES de cortar a energia.
#ifdef __cplusplus
extern "C" {
#endif
void power_shutdown(void) __attribute__((noreturn));
void power_reboot(void)   __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
