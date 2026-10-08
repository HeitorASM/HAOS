// tests/perm/stubs.c — substitutos de host para o que o kernel fornece
// (memória, strings, disco, relógio, idioma). As funções de string são
// CÓPIAS das de kernel/memory.cpp (mesma semântica, inclusive kstrncpy que
// não garante o '\0'), para o teste exercitar o mesmo comportamento.
#include <stdlib.h>
#include <string.h>
#include "../../kernel/types.h"
#include "../../kernel/memory.h"
#include "../../kernel/lang.h"
#include "../../fs/haosfs.h"
#include "../../drivers/ata_pio.h"
#include "../../drivers/rtc.h"

void* kmalloc(size_t n) { return malloc(n ? n : 1); }
void* kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void  kfree(void* p)    { free(p); }

void* kmemset(void* dst, int val, size_t n) { uint8_t* d = dst; while (n--) *d++ = (uint8_t)val; return dst; }
void* kmemcpy(void* dst, const void* src, size_t n) { return memcpy(dst, src, n); }
int kstrcmp(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
int kstrncmp(const char* a, const char* b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}
size_t kstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
char* kstrcpy(char* dst, const char* src) { char* d = dst; while ((*d++ = *src++)); return dst; }
char* kstrncpy(char* dst, const char* src, size_t n) { char* d = dst; while (n && (*d++ = *src++)) n--; if (n) *d = '\0'; return dst; }
char* kstrcat(char* dst, const char* src) { char* d = dst; while (*d) d++; while ((*d++ = *src++)); return dst; }
char* kstrncat(char* dst, const char* src, size_t dst_size) {
    if (dst_size == 0) return dst;
    size_t len = 0;
    while (len < dst_size && dst[len]) len++;
    if (len >= dst_size - 1) { dst[dst_size - 1] = '\0'; return dst; }
    size_t i = 0;
    while (src[i] && (len + i) < dst_size - 1) { dst[len + i] = src[i]; i++; }
    dst[len + i] = '\0';
    return dst;
}
void kuitoa(uint64_t n, char* buf) {
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i = 0;
    while (n) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}
void kitoa(int64_t n, char* buf) { if (n < 0) { buf[0]='-'; kuitoa((uint64_t)-n, buf+1); } else kuitoa((uint64_t)n, buf); }

// ---- disco: "persistente" de mentira que só conta as gravações ----
int g_save_count = 0;
BlockDevice* ata_pio_device(void) { return NULL; }
BlockDevice* storage_device(void)  { return NULL; }
HaosFsProbe  haosfs_probe(BlockDevice* d)  { (void)d; return HAOSFS_EMPTY; }
bool         haosfs_format(BlockDevice* d) { (void)d; return 1; }
bool         haosfs_mount(BlockDevice* d, VfsNode* r) { (void)d; (void)r; return 0; }
bool         haosfs_save(BlockDevice* d, VfsNode* r)  { (void)d; (void)r; g_save_count++; return 1; }

volatile uint64_t timer_ticks = 12345;
void rtc_read_time(rtc_time_t* t) { memset(t, 0, sizeof(*t)); t->second = (uint8_t)(timer_ticks++ & 59); }

const char* tr(StrID id) { return id == STR_USER_README_TEXT ? "readme de teste\n" : "?"; }
