#pragma once
#include "types.h"

// ============================================================
//  kernel/sha256.h — SHA-256, HMAC-SHA-256 e PBKDF2-HMAC-SHA-256
//
//  Usado pelo banco de usuários (kernel/user.c) para guardar senhas
//  com salt + derivação de chave custosa, em vez do FNV-1a de 32 bits
//  que o login usava antes (trivial de quebrar por força bruta).
//
//  Implementação independente de kernel: só depende de kmemcpy /
//  kmemset (kernel/memory.h). NÃO usa memcpy/memset "de verdade"
//  porque o kernel não os define e o GCC pode gerar chamadas
//  implícitas a eles para cópias de struct.
//
//  Validada contra os vetores de teste públicos (NIST para SHA-256,
//  RFC 7914 / RFC 6070-estilo para PBKDF2-HMAC-SHA256) — ver
//  tests/crypto/run.sh.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;      // total de bits já processados
    uint8_t  buf[64];     // bloco parcial
    uint32_t buflen;      // bytes usados em buf
} sha256_ctx;

void sha256_init(sha256_ctx* c);
void sha256_update(sha256_ctx* c, const void* data, size_t len);
void sha256_final(sha256_ctx* c, uint8_t out[SHA256_DIGEST_SIZE]);

// Atalho: hash de um buffer só.
void sha256(const void* data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]);

// PBKDF2-HMAC-SHA256, saída fixa de 32 bytes (1 bloco — suficiente para
// armazenar senha). `iterations` precisa ser >= 1.
void pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                        const uint8_t* salt, size_t salt_len,
                        uint32_t iterations,
                        uint8_t out[SHA256_DIGEST_SIZE]);

// Comparação em tempo constante (não vaza, pelo tempo, em qual byte
// os buffers divergem). Retorna 1 se iguais, 0 se diferentes.
int ct_equal(const void* a, const void* b, size_t n);

#ifdef __cplusplus
}
#endif
