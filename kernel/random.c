#include "random.h"
#include "sha256.h"
#include "memory.h"
#include "../drivers/rtc.h"

extern volatile uint64_t timer_ticks;     // 100 Hz, IRQ0

static uint8_t  g_key[SHA256_DIGEST_SIZE];  // estado secreto do DRBG
static uint64_t g_counter;
static uint32_t g_entropy_bits;
static bool     g_init;
static int      g_has_rdseed, g_has_rdrand;

static inline uint64_t rd_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpuid_leaf(uint32_t leaf, uint32_t sub,
                              uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                             : "a"(leaf), "c"(sub));
}

// ok=1 quando a instrução entregou um valor válido (CF=1). Pode falhar
// transitoriamente — quem chama tenta de novo.
static inline int hw_rdrand64(uint64_t* v) {
    uint8_t ok;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok) :: "cc");
    return ok;
}
static inline int hw_rdseed64(uint64_t* v) {
    uint8_t ok;
    __asm__ volatile("rdseed %0; setc %1" : "=r"(*v), "=qm"(ok) :: "cc");
    return ok;
}

// key = SHA256(key || dados)
static void mix(const void* data, size_t len) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, g_key, sizeof(g_key));
    sha256_update(&c, data, len);
    sha256_final(&c, g_key);
}

// Jitter: o TSC medido depois de curtas esperas varia com cache, SMI,
// frequência... Em VM vem do host; em ferro real vem do próprio silício.
// Crédito conservador: 1 bit por 8 amostras.
static void collect_jitter(void) {
    uint64_t samples[64];
    for (int i = 0; i < 64; i++) {
        uint64_t t0 = rd_tsc();
        volatile uint32_t spin = 0;
        for (int k = 0; k < 200 + (i & 7) * 13; k++) spin += (uint32_t)k;
        samples[i] = rd_tsc() - t0;
    }
    mix(samples, sizeof(samples));
    g_entropy_bits += 8;
}

static void collect_hw(void) {
    uint64_t buf[8];
    int n = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t v = 0;
        int ok = 0;
        for (int tries = 0; tries < 16 && !ok; tries++) {
            if (g_has_rdseed)      ok = hw_rdseed64(&v);
            else if (g_has_rdrand) ok = hw_rdrand64(&v);
            else break;
        }
        if (ok) buf[n++] = v;
    }
    if (n) {
        mix(buf, (size_t)n * sizeof(uint64_t));
        g_entropy_bits += (uint32_t)n * 64u;
        kmemset(buf, 0, sizeof(buf));
    }
}

static void rng_init_once(void) {
    if (g_init) return;
    g_init = true;                      // antes de tudo: evita recursão

    uint32_t a, b, c, d;
    cpuid_leaf(0, 0, &a, &b, &c, &d);
    uint32_t maxleaf = a;
    if (maxleaf >= 1) {
        cpuid_leaf(1, 0, &a, &b, &c, &d);
        g_has_rdrand = (c >> 30) & 1u;
    }
    if (maxleaf >= 7) {
        cpuid_leaf(7, 0, &a, &b, &c, &d);
        g_has_rdseed = (b >> 18) & 1u;
    }

    rtc_time_t rt;
    kmemset(&rt, 0, sizeof(rt));
    rtc_read_time(&rt);
    uint64_t t = timer_ticks, tsc = rd_tsc();
    mix(&rt, sizeof(rt));
    mix(&t, sizeof(t));
    mix(&tsc, sizeof(tsc));
    g_entropy_bits += 4;                // relógio: quase nada de entropia

    collect_hw();
    collect_jitter();
}

void rng_add_entropy(const void* data, size_t len, uint32_t bits) {
    rng_init_once();
    mix(data, len);
    g_entropy_bits += bits;
    if (g_entropy_bits > 4096) g_entropy_bits = 4096;
}

void rng_add_event(uint32_t tag) {
    // Chamado de IRQ: sem init pesado aqui — só mistura se já inicializado.
    if (!g_init) return;
    uint64_t rec[2] = { rd_tsc(), tag };
    mix(rec, sizeof(rec));
    if (g_entropy_bits < 4096) g_entropy_bits++;
}

bool rng_is_seeded(void) {
    rng_init_once();
    return g_entropy_bits >= 128;
}

int rng_has_hw(void) {
    rng_init_once();
    return g_has_rdseed || g_has_rdrand;
}

void rng_bytes(void* out, size_t n) {
    rng_init_once();
    uint8_t* p = (uint8_t*)out;

    // Reforço barato a cada chamada: hardware (se houver) + TSC atual.
    if (g_has_rdseed || g_has_rdrand) collect_hw();
    uint64_t tsc = rd_tsc();
    mix(&tsc, sizeof(tsc));

    while (n) {
        uint8_t blk[SHA256_DIGEST_SIZE];
        sha256_ctx c;
        sha256_init(&c);
        sha256_update(&c, g_key, sizeof(g_key));
        sha256_update(&c, &g_counter, sizeof(g_counter));
        sha256_update(&c, "out", 3);
        sha256_final(&c, blk);
        g_counter++;

        size_t take = n < sizeof(blk) ? n : sizeof(blk);
        kmemcpy(p, blk, take);
        kmemset(blk, 0, sizeof(blk));
        p += take; n -= take;
    }

    // Ratchet: troca a chave por um derivado — a saída antiga não pode ser
    // reconstruída a partir de um vazamento futuro do estado.
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, g_key, sizeof(g_key));
    sha256_update(&c, "ratchet", 7);
    sha256_final(&c, g_key);
}
