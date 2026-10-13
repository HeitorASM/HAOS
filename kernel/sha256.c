#include "sha256.h"
#include "memory.h"

// ---- Constantes (frações das raízes cúbicas / quadradas dos primeiros
//      primos, FIPS 180-4) — geradas por script, não digitadas à mão ----
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static const uint32_t H256_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x) (ROTR(x, 2)  ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSIG1(x) (ROTR(x, 6)  ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSIG0(x) (ROTR(x, 7)  ^ ROTR(x, 18) ^ ((x) >> 3))
#define SSIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx* c, const uint8_t block[64]) {
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4]     << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] <<  8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (; i < 64; i++)
        w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];

    a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
    e = c->state[4]; f = c->state[5]; g  = c->state[6]; h = c->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + BSIG1(e) + CH(e, f, g) + K256[i] + w[i];
        t2 = BSIG0(a) + MAJ(a, b, cc);
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g;  c->state[7] += h;

    // Não deixa o schedule de mensagem (derivado da senha) na pilha.
    kmemset(w, 0, sizeof(w));
}

void sha256_init(sha256_ctx* c) {
    kmemcpy(c->state, H256_INIT, sizeof(c->state));
    c->bitlen = 0;
    c->buflen = 0;
    kmemset(c->buf, 0, sizeof(c->buf));
}

void sha256_update(sha256_ctx* c, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        size_t room = 64 - c->buflen;
        size_t n = len < room ? len : room;
        kmemcpy(c->buf + c->buflen, p, n);
        c->buflen += (uint32_t)n;
        p   += n;
        len -= n;
        if (c->buflen == 64) {
            sha256_transform(c, c->buf);
            c->bitlen += 512;
            c->buflen = 0;
        }
    }
}

void sha256_final(sha256_ctx* c, uint8_t out[SHA256_DIGEST_SIZE]) {
    uint64_t total_bits = c->bitlen + (uint64_t)c->buflen * 8;

    // Padding: 0x80, zeros até sobrar 8 bytes, comprimento em bits (big-endian).
    // (kmemset explícito em vez de laços: o kernel não tem memset/memcpy
    // "de verdade" e o GCC pode transformar um laço de zeragem numa chamada
    // a memset, que não existiria no link.)
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        kmemset(c->buf + c->buflen, 0, 64 - c->buflen);
        sha256_transform(c, c->buf);
        c->buflen = 0;
    }
    kmemset(c->buf + c->buflen, 0, 56 - c->buflen);
    c->buflen = 56;
    for (int i = 0; i < 8; i++)
        c->buf[56 + i] = (uint8_t)(total_bits >> (56 - 8 * i));
    sha256_transform(c, c->buf);

    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
    kmemset(c, 0, sizeof(*c));
}

void sha256(const void* data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, out);
}

// ---- PBKDF2-HMAC-SHA256 (RFC 8018), 1 bloco de saída -------------------
//
// HMAC(K, m) = H((K^opad) || H((K^ipad) || m)). Como K é a MESMA senha em
// todas as iterações, o estado do hash após absorver (K^ipad) e (K^opad)
// é calculado UMA vez e copiado a cada iteração — cada iteração custa
// só 2 compressões SHA-256 em vez de 4.
void pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                        const uint8_t* salt, size_t salt_len,
                        uint32_t iterations,
                        uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t key[64];
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t u[32];
    uint8_t t[32];
    static const uint8_t block_index[4] = { 0, 0, 0, 1 };
    sha256_ctx in0, out0, w;

    if (iterations < 1) iterations = 1;

    kmemset(key, 0, sizeof(key));
    if (password_len > 64) sha256(password, password_len, key);
    else if (password_len > 0) kmemcpy(key, password, password_len);

    for (int i = 0; i < 64; i++) {
        ipad[i] = (uint8_t)(key[i] ^ 0x36);
        opad[i] = (uint8_t)(key[i] ^ 0x5c);
    }

    sha256_init(&in0);  sha256_update(&in0,  ipad, 64);
    sha256_init(&out0); sha256_update(&out0, opad, 64);

    // U1 = HMAC(P, salt || INT_32_BE(1))
    kmemcpy(&w, &in0, sizeof(w));
    sha256_update(&w, salt, salt_len);
    sha256_update(&w, block_index, 4);
    sha256_final(&w, u);

    kmemcpy(&w, &out0, sizeof(w));
    sha256_update(&w, u, 32);
    sha256_final(&w, u);

    kmemcpy(t, u, 32);

    for (uint32_t it = 1; it < iterations; it++) {
        kmemcpy(&w, &in0, sizeof(w));
        sha256_update(&w, u, 32);
        sha256_final(&w, u);

        kmemcpy(&w, &out0, sizeof(w));
        sha256_update(&w, u, 32);
        sha256_final(&w, u);

        for (int j = 0; j < 32; j++) t[j] ^= u[j];
    }

    kmemcpy(out, t, 32);

    // Apaga o que derivou da senha.
    kmemset(key,  0, sizeof(key));
    kmemset(ipad, 0, sizeof(ipad));
    kmemset(opad, 0, sizeof(opad));
    kmemset(u,    0, sizeof(u));
    kmemset(t,    0, sizeof(t));
    kmemset(&in0,  0, sizeof(in0));
    kmemset(&out0, 0, sizeof(out0));
    kmemset(&w,    0, sizeof(w));
}

int ct_equal(const void* a, const void* b, size_t n) {
    const volatile uint8_t* x = (const volatile uint8_t*)a;
    const volatile uint8_t* y = (const volatile uint8_t*)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff == 0;
}
