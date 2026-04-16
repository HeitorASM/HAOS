#include "memory.h"
#include "types.h"

extern uint8_t _kernel_end[];
static uint8_t* heap_ptr = NULL;
static uint8_t* heap_end = NULL;

// Heap de 12 MB para suportar: shadow buffer (~3MB) + cache de fundo (~3MB) + resto
#define HEAP_SIZE (12 * 1024 * 1024)

void memory_init(void) {
    heap_ptr = _kernel_end;
    heap_end = heap_ptr + HEAP_SIZE;
}

void* kmalloc(size_t n) {
    n = (n + 7) & ~7ULL;
    if (!heap_ptr || heap_ptr + n > heap_end) return NULL;
    void* p = heap_ptr;
    heap_ptr += n;
    return p;
}

void* kzalloc(size_t n) {
    void* p = kmalloc(n);
    if (p) { uint8_t* b = p; for (size_t i = 0; i < n; i++) b[i] = 0; }
    return p;
}

void kfree(void* p) { (void)p; }

void* kmemset(void* dst, int val, size_t n) {
    uint8_t* d = dst;
    while (n--) *d++ = (uint8_t)val;
    return dst;
}

void* kmemcpy(void* dst, const void* src, size_t n) {
    // Cópia rápida em blocos de 8 bytes
    uint64_t* d8 = (uint64_t*)dst;
    const uint64_t* s8 = (const uint64_t*)src;
    size_t n8 = n / 8;
    for (size_t i = 0; i < n8; i++) d8[i] = s8[i];
    // Restante
    uint8_t* db = (uint8_t*)(d8 + n8);
    const uint8_t* sb = (const uint8_t*)(s8 + n8);
    for (size_t i = 0; i < (n & 7); i++) db[i] = sb[i];
    return dst;
}

int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

size_t kstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

char* kstrcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* kstrcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

void kitoa(int64_t n, char* buf) {
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i=0;
    bool neg = n < 0;
    if (neg) n = -n;
    while (n) { tmp[i++] = '0' + (n % 10); n /= 10; }
    if (neg) tmp[i++] = '-';
    for (int j=0; j<i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}

void kuitoa_hex(uint64_t n, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x';
    for (int i=0; i<16; i++)
        buf[2+i] = hex[(n >> (60 - i*4)) & 0xF];
    buf[18] = 0;
}
