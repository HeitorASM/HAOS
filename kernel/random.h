#pragma once
#include "types.h"

// ============================================================
//  kernel/random.h — gerador de números pseudo-aleatórios do kernel
//
//  Pool de entropia + DRBG baseado em SHA-256 (sem estado reaproveitável:
//  cada saída "gira" a chave, então vazar o estado hoje não revela o que
//  foi gerado ontem).
//
//  Fontes de entropia:
//    * RDSEED / RDRAND (detectados via CPUID) — hardware real e VirtualBox
//    * jitter do TSC medido ao redor de espera do PIT
//    * RTC + ticks do PIT
//    * eventos de teclado/mouse (rng_add_event) — o melhor que há sem HW RNG
//
//  Inicialização é preguiçosa: rng_bytes() já se auto-inicializa.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Preenche `out` com n bytes (qualquer n). Nunca falha.
void rng_bytes(void* out, size_t n);

// Mistura dados externos no pool (ex.: tempo entre teclas). `bits` é a
// estimativa CONSERVADORA de entropia que eles trazem (0 = só mistura).
void rng_add_entropy(const void* data, size_t len, uint32_t bits);

// Atalho para IRQs: mistura TSC + um rótulo (ex.: scancode). Barato; pode
// ser chamado dentro de handlers de interrupção. Conta ~1 bit por chamada.
void rng_add_event(uint32_t tag);

// true quando a estimativa de entropia já passou de 128 bits (ou há
// RDSEED/RDRAND). Para chaves de VERDADE, espere isto; para salt, tanto faz.
bool rng_is_seeded(void);

// 1 se a CPU tem instrução de RNG por hardware (RDSEED ou RDRAND).
int  rng_has_hw(void);

#ifdef __cplusplus
}
#endif
