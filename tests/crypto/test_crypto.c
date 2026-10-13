// tests/crypto/test_crypto.c — teste de HOST (não roda no HAOS).
//
// Valida kernel/sha256.c contra vetores de teste públicos. Compila só
// sha256.c + este arquivo (não o kernel inteiro):  bash tests/crypto/run.sh
#include <stdio.h>
#include <string.h>
#undef NULL
#include "../../kernel/types.h"
#include "../../kernel/sha256.h"

// Stubs de kernel/memory.h (sha256.c só usa estes dois).
void* kmemcpy(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
void* kmemset(void* d, int v, size_t n)         { return memset(d, v, n); }

static int fails = 0;

static void hex(const uint8_t* d, size_t n, char* out) {
    static const char* h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[i*2] = h[d[i] >> 4]; out[i*2+1] = h[d[i] & 15]; }
    out[n*2] = 0;
}

static void check(const char* name, const uint8_t* got, const char* want_hex) {
    char g[65]; hex(got, 32, g);
    if (strcmp(g, want_hex) == 0) { printf("  ok   %s\n", name); }
    else { printf("  FAIL %s\n    got  %s\n    want %s\n", name, g, want_hex); fails++; }
}

int main(void) {
    uint8_t d[32];

    // ---- SHA-256 (FIPS 180-4 / NIST) ----
    sha256("", 0, d);
    check("sha256(\"\")", d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256("abc", 3, d);
    check("sha256(\"abc\")", d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(m2, strlen(m2), d);
    check("sha256(448-bit msg)", d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // 1.000.000 x 'a' em pedaços (exercita update com buffer parcial)
    sha256_ctx c; sha256_init(&c);
    char chunk[1000]; memset(chunk, 'a', sizeof(chunk));
    for (int i = 0; i < 1000; i++) sha256_update(&c, chunk, sizeof(chunk));
    sha256_final(&c, d);
    check("sha256(1e6 x 'a')", d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Fronteiras de padding do SHA-256 (55/56/63/64/65/119/120 bytes de 'x'),
    // valores de referência gerados com hashlib do Python.
    {
        char buf[130]; memset(buf, 'x', sizeof(buf));
        sha256(buf, 55, d); check("sha256 len 55", d, "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072");
        sha256(buf, 56, d); check("sha256 len 56", d, "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e");
        sha256(buf, 63, d); check("sha256 len 63", d, "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2");
        sha256(buf, 64, d); check("sha256 len 64", d, "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c");
        sha256(buf, 65, d); check("sha256 len 65", d, "9537c5fdf120482f7d58d25e9ed583f52c02b4e304ea814db1633ad565aed7e9");
        sha256(buf, 119, d); check("sha256 len 119", d, "000b48d4edf0fa7bee3c6236ecd2785baa5db4eeb8bb54341b029e0d9fa5fb0c");
        sha256(buf, 120, d); check("sha256 len 120", d, "13f05a0b594787f5ecd315edc96141bd3243203d1b7d4f0836f37308b276ba98");
    }

    // ---- PBKDF2-HMAC-SHA256 (vetores amplamente publicados) ----
    pbkdf2_hmac_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, d);
    check("pbkdf2 c=1", d, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    pbkdf2_hmac_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 2, d);
    check("pbkdf2 c=2", d, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    pbkdf2_hmac_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 4096, d);
    check("pbkdf2 c=4096", d, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");

    // Senha > 64 bytes (a chave HMAC é hasheada antes de usar) — referência: hashlib.
    {
        uint8_t pw[100]; memset(pw, 'k', sizeof(pw));
        pbkdf2_hmac_sha256(pw, sizeof(pw), (const uint8_t*)"salt", 4, 3, d);
        check("pbkdf2 senha de 100 bytes", d, "219c68bb88f1010f4a763b7c8fd58639c2280baf6820814c6d404891fcd85c3d");
    }

    // ---- ct_equal ----
    uint8_t a[4] = {1,2,3,4}, b[4] = {1,2,3,4}, e[4] = {1,2,3,5};
    if (ct_equal(a, b, 4) && !ct_equal(a, e, 4) && ct_equal(a, e, 3)) printf("  ok   ct_equal\n");
    else { printf("  FAIL ct_equal\n"); fails++; }

    printf(fails ? "\n%d FALHA(S)\n" : "\ntodos os testes passaram\n", fails);
    return fails ? 1 : 0;
}
