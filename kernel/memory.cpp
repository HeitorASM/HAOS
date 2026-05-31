// ============================================================
//  memory.cpp — Gestor de Memória HAOS
//
//  Módulo 1: PageFrameAllocator (PFA) baseado em Bitmap
//  Módulo 2: KernelHeap (Free-List Encadeada com coalescing real)
//  Módulo 3: Operadores globais new / delete
//  Módulo 4: API C (kmalloc/kfree/kzalloc + utilitários de string)
// ============================================================

#include "memory.h"
#include "multiboot2.h"
#include "types.h"

// ============================================================
//  Constantes
// ============================================================
static constexpr uint64_t PAGE_SIZE       = 4096;
static constexpr uint64_t HEAP_INIT_PAGES = 256;   // 1 MB de heap inicial

// ============================================================
//  MÓDULO 1 — PageFrameAllocator (PFA) por Bitmap
// ============================================================

extern uint8_t _kernel_end[];  // símbolo do linker

namespace {

// Bitmap estático no BSS: 1 bit por página de 4KB.
// Para cobrir 4 GB: 4GB / 4KB / 8bits = 131 072 bytes.
static constexpr uint64_t MAX_PAGES    = (4ULL * 1024 * 1024 * 1024) / PAGE_SIZE;
static constexpr uint64_t BITMAP_BYTES = MAX_PAGES / 8;

static uint8_t  s_bitmap[BITMAP_BYTES];
static uint64_t s_total_pages = 0;
static uint64_t s_free_pages  = 0;
static uint64_t s_total_bytes = 0;

// ---- Operações de bitmap ----
static inline void bitmap_set(uint64_t frame) {
    s_bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}
static inline void bitmap_clear(uint64_t frame) {
    s_bitmap[frame / 8] &= (uint8_t)(~(1u << (frame % 8)));
}
static inline bool bitmap_test(uint64_t frame) {
    return (bool)((s_bitmap[frame / 8] >> (frame % 8)) & 1u);
}

// ---- Inicializa o PFA com o mmap do Multiboot2 ----
static void pfa_init(uint32_t mb_info_raw) {
    // Começa com tudo marcado como "usado"
    for (uint64_t i = 0; i < BITMAP_BYTES; i++) s_bitmap[i] = 0xFF;

    uint8_t*   ptr = (uint8_t*)(uintptr_t)(mb_info_raw + 8);
    mb2_tag_t* tag = (mb2_tag_t*)ptr;

    while (tag->type != MB2_TAG_END) {
        if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t* mt  = (mb2_tag_mmap_t*)tag;
            uint32_t  entry_sz  = mt->entry_size;
            uint8_t*  ep        = (uint8_t*)mt + sizeof(mb2_tag_mmap_t);
            uint8_t*  ep_end    = (uint8_t*)mt + mt->size;

            while (ep < ep_end) {
                mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)ep;

                if (e->type == MB2_MMAP_AVAILABLE) {
                    s_total_bytes += e->length;

                    uint64_t start = e->base_addr / PAGE_SIZE;
                    uint64_t count = e->length    / PAGE_SIZE;
                    for (uint64_t f = start; f < start + count && f < MAX_PAGES; f++) {
                        if (bitmap_test(f)) {
                            bitmap_clear(f);
                            s_free_pages++;
                        }
                    }
                    uint64_t end = start + count;
                    if (end > s_total_pages) s_total_pages = end;
                }
                ep += entry_sz;
            }
        }

        uint32_t sz = tag->size;
        if (sz & 7) sz = (sz + 7) & ~7u;
        if (sz < 8) break;
        tag = (mb2_tag_t*)((uint8_t*)tag + sz);
    }

    // Reserva: página 0 (BIOS)
    if (!bitmap_test(0)) { bitmap_set(0); if (s_free_pages) s_free_pages--; }

    // Reserva: páginas do kernel (1MB → _kernel_end)
    uint64_t kstart = 0x100000ULL / PAGE_SIZE;
    uint64_t kend   = ((uintptr_t)_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t f = kstart; f < kend && f < MAX_PAGES; f++) {
        if (!bitmap_test(f)) { bitmap_set(f); if (s_free_pages) s_free_pages--; }
    }
}

// ---- Aloca uma página física (retorna endereço físico, 0 = OOM) ----
static uint64_t pfa_alloc_page() {
    for (uint64_t byte = 0; byte < BITMAP_BYTES; byte++) {
        if (s_bitmap[byte] == 0xFF) continue;
        for (uint8_t bit = 0; bit < 8; bit++) {
            uint64_t frame = byte * 8 + (uint64_t)bit;
            if (frame >= s_total_pages) return 0;
            if (!bitmap_test(frame)) {
                bitmap_set(frame);
                if (s_free_pages) s_free_pages--;
                return frame * PAGE_SIZE;
            }
        }
    }
    return 0;
}

// ---- Liberta uma página física ----
static void pfa_free_page(uint64_t phys) {
    uint64_t frame = phys / PAGE_SIZE;
    if (frame < MAX_PAGES && bitmap_test(frame)) {
        bitmap_clear(frame);
        s_free_pages++;
    }
}

} // namespace anon

// ============================================================
//  MÓDULO 2 — KernelHeap (Free-List Encadeada)
// ============================================================

namespace {

struct HeapBlock {
    size_t     size;    // tamanho dos DADOS (não inclui o header)
    bool       free;
    HeapBlock* next;
    uint32_t   magic;   // 0xDEADBEEF — detecção de corrupção
};

static constexpr uint32_t HEAP_MAGIC     = 0xDEADBEEFu;
static constexpr size_t   BLOCK_ALIGN    = 16;
static constexpr size_t   MIN_SPLIT_SIZE = 64;

static HeapBlock* s_heap_head  = nullptr;
static uint64_t   s_heap_used  = 0;
static uint64_t   s_heap_total = 0;

static inline size_t align_up(size_t n, size_t a) {
    return (n + a - 1) & ~(a - 1);
}

// ---- Adiciona um bloco livre à lista (inserção ordenada por endereço) ----
static void heap_add_block(HeapBlock* blk) {
    blk->next = nullptr;
    if (!s_heap_head) {
        s_heap_head = blk;
        return;
    }
    // Inserção ordenada por endereço crescente
    HeapBlock* cur  = s_heap_head;
    HeapBlock* prev = nullptr;
    while (cur && (uintptr_t)cur < (uintptr_t)blk) {
        prev = cur;
        cur  = cur->next;
    }
    blk->next = cur;
    if (prev) prev->next = blk;
    else      s_heap_head = blk;
}

// ---- Expande a heap obtendo páginas contíguas do PFA ----
static bool heap_expand(uint64_t num_pages) {
    uint64_t base = pfa_alloc_page();
    if (!base) return false;

    uint64_t total = PAGE_SIZE;
    uint64_t last  = base;

    for (uint64_t i = 1; i < num_pages; i++) {
        uint64_t next = pfa_alloc_page();
        if (!next) break;
        if (next != last + PAGE_SIZE) { pfa_free_page(next); break; }
        last   = next;
        total += PAGE_SIZE;
    }

    HeapBlock* blk = (HeapBlock*)(uintptr_t)base;
    blk->size  = total - sizeof(HeapBlock);
    blk->free  = true;
    blk->magic = HEAP_MAGIC;
    heap_add_block(blk);
    s_heap_total += total;
    return true;
}

// ---- Funde blocos livres adjacentes (coalescing) ----
static void heap_coalesce() {
    HeapBlock* cur = s_heap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            uintptr_t cur_end = (uintptr_t)cur + sizeof(HeapBlock) + cur->size;
            if (cur_end == (uintptr_t)cur->next) {
                cur->size += sizeof(HeapBlock) + cur->next->size;
                cur->next  = cur->next->next;
                continue;  // tenta fundir mais
            }
        }
        cur = cur->next;
    }
}

// ---- Aloca n bytes ----
static void* heap_alloc(size_t n) {
    if (n == 0) return nullptr;
    n = align_up(n, BLOCK_ALIGN);

    // First-fit
    HeapBlock* cur = s_heap_head;
    while (cur) {
        if (cur->free && cur->magic == HEAP_MAGIC && cur->size >= n) {
            // Divide o bloco se sobrar espaço suficiente
            if (cur->size >= n + sizeof(HeapBlock) + MIN_SPLIT_SIZE) {
                HeapBlock* split = (HeapBlock*)((uint8_t*)cur + sizeof(HeapBlock) + n);
                split->size  = cur->size - n - sizeof(HeapBlock);
                split->free  = true;
                split->next  = cur->next;
                split->magic = HEAP_MAGIC;
                cur->next    = split;
                cur->size    = n;
            }
            cur->free  = false;
            s_heap_used += cur->size;
            return (void*)((uint8_t*)cur + sizeof(HeapBlock));
        }
        cur = cur->next;
    }

    // Sem bloco livre — expande e tenta uma vez mais
    uint64_t pages = (n + sizeof(HeapBlock) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < 4) pages = 4;
    if (!heap_expand(pages)) return nullptr;

    // Segunda tentativa (apenas first-fit, sem recursão)
    cur = s_heap_head;
    while (cur) {
        if (cur->free && cur->magic == HEAP_MAGIC && cur->size >= n) {
            if (cur->size >= n + sizeof(HeapBlock) + MIN_SPLIT_SIZE) {
                HeapBlock* split = (HeapBlock*)((uint8_t*)cur + sizeof(HeapBlock) + n);
                split->size  = cur->size - n - sizeof(HeapBlock);
                split->free  = true;
                split->next  = cur->next;
                split->magic = HEAP_MAGIC;
                cur->next    = split;
                cur->size    = n;
            }
            cur->free  = false;
            s_heap_used += cur->size;
            return (void*)((uint8_t*)cur + sizeof(HeapBlock));
        }
        cur = cur->next;
    }
    return nullptr;  // OOM
}

// ---- Liberta um bloco ----
static void heap_free(void* ptr) {
    if (!ptr) return;
    HeapBlock* blk = (HeapBlock*)((uint8_t*)ptr - sizeof(HeapBlock));
    if (blk->magic != HEAP_MAGIC || blk->free) return;
    blk->free = true;
    if (s_heap_used >= blk->size) s_heap_used -= blk->size;
    heap_coalesce();
}

} 

// ============================================================
//  MÓDULO 3 — Operadores globais new / delete
//
//  NOTA TÉCNICA: Em x86_64-linux-gnu, size_t = unsigned long.
//  O compilador exige que operator new receba exactamente esse tipo.
//  Usamos __SIZE_TYPE__ em types.h para garantir a correspondência.
// ============================================================

void* operator new(size_t size) {
    return heap_alloc(size);
}

void* operator new[](size_t size) {
    return heap_alloc(size);
}

void operator delete(void* ptr) noexcept {
    heap_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    heap_free(ptr);
}

// Versões "sized" (C++14) — o compilador pode gerar chamadas a estas
void operator delete(void* ptr, size_t) noexcept {
    heap_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    heap_free(ptr);
}

// ============================================================
//  MÓDULO 4 — API C e Inicialização
// ============================================================

extern "C" {

void memory_init(uint32_t mb_info_raw) {
    pfa_init(mb_info_raw);
    heap_expand(HEAP_INIT_PAGES);
}

void* kmalloc(size_t n) { return heap_alloc(n); }

void* kzalloc(size_t n) {
    void* p = heap_alloc(n);
    if (p) {
        uint8_t* b = (uint8_t*)p;
        for (size_t i = 0; i < n; i++) b[i] = 0;
    }
    return p;
}

void kfree(void* p) { heap_free(p); }

void* kmemset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)val;
    return dst;
}

void* kmemcpy(void* dst, const void* src, size_t n) {
    uint64_t*       d8 = (uint64_t*)dst;
    const uint64_t* s8 = (const uint64_t*)src;
    size_t n8 = n / 8;
    for (size_t i = 0; i < n8; i++) d8[i] = s8[i];
    uint8_t*       db = (uint8_t*)(d8 + n8);
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

size_t kstrlen(const char* s) {
    size_t n = 0; while (s[n]) n++; return n;
}

char* kstrcpy(char* dst, const char* src) {
    char* d = dst; while ((*d++ = *src++)); return dst;
}

char* kstrncpy(char* dst, const char* src, size_t n) {
    char* d = dst;
    while (n && (*d++ = *src++)) n--;
    if (n) *d = '\0';
    return dst;
}

char* kstrcat(char* dst, const char* src) {
    char* d = dst; while (*d) d++; while ((*d++ = *src++)); return dst;
}

void kitoa(int64_t n, char* buf) {
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i = 0;
    bool neg = n < 0; if (neg) n = -n;
    while (n) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    if (neg) tmp[i++] = '-';
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}

void kuitoa(uint64_t n, char* buf) {
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[24]; int i = 0;
    while (n) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}

void kuitoa_hex(uint64_t n, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x';
    for (int i = 0; i < 16; i++)
        buf[2+i] = hex[(n >> (60 - i*4)) & 0xF];
    buf[18] = 0;
}

uint64_t mem_get_total_bytes()  { return s_total_bytes; }
uint64_t mem_get_free_pages()   { return s_free_pages; }
uint64_t mem_get_used_pages()   { return s_total_pages > s_free_pages ? s_total_pages - s_free_pages : 0; }
uint64_t mem_get_heap_used()    { return s_heap_used; }

} 