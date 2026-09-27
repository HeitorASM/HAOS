#include "stdlib.h"
#include "unistd.h"
#include "string.h"

// ============================================================
//  libc/src/stdlib/malloc.c — alocador first-fit simples
//
//  Cada bloco (livre ou em uso) tem um cabeçalho (block_header_t)
//  imediatamente antes dos bytes que o chamador recebe. O ponteiro
//  que malloc() retorna é sempre (header + 1) — ou seja, logo após
//  o cabeçalho; free() faz o caminho inverso, (block_header_t*)ptr - 1,
//  para reencontrar o cabeçalho a partir do ponteiro do usuário.
//
//  Blocos livres formam uma lista encadeada simples (free_list),
//  ordenada por ENDEREÇO crescente — manter essa ordem é o que torna
//  barato checar se dois blocos livres são ADJACENTES na memória
//  (coalescing) ao liberar um bloco: só precisamos olhar o vizinho
//  anterior e o próximo na lista, nunca escanear tudo.
// ============================================================

typedef struct block_header {
    size_t                size;    // bytes de PAYLOAD (sem contar o
                                     // próprio cabeçalho) que este
                                     // bloco tem disponível.
    struct block_header*  next;    // próximo bloco na free_list (só
                                     // válido enquanto o bloco está
                                     // LIVRE — uma vez alocado, este
                                     // campo não tem significado,
                                     // mas seu espaço continua
                                     // reservado no cabeçalho para
                                     // simplificar: todo bloco, livre
                                     // ou não, tem o mesmo tamanho de
                                     // cabeçalho).
    int                    in_use; // 0 = livre, 1 = alocado.
} block_header_t;

#define ALIGNMENT           16u    // alinhamento mínimo de qualquer
                                     // alocação — suficiente para
                                     // qualquer tipo básico de C.
#define MIN_BLOCK_PAYLOAD   32u    // todo bloco tem pelo menos este
                                     // tanto de payload, mesmo que o
                                     // pedido original seja menor —
                                     // evita fragmentar a heap em
                                     // blocos minúsculos demais para
                                     // serem reaproveitados depois.

static block_header_t* free_list_head = NULL;
static void*           heap_start     = NULL; // primeiro endereço
                                                 // desta heap (obtido
                                                 // via sbrk(0) na
                                                 // primeira chamada).

static size_t align_up(size_t n, size_t alignment) {
    return (n + alignment - 1) & ~(alignment - 1);
}

// Pede mais memória ao kernel via sbrk() e a formata como um novo
// bloco (ainda não inserido em nenhuma lista — quem chama decide o
// que fazer com ele). `payload_size` já deve estar alinhado.
static block_header_t* request_from_kernel(size_t payload_size) {
    size_t total = sizeof(block_header_t) + payload_size;
    void* mem = sbrk((long)total);
    if (mem == (void*)-1) return NULL;

    block_header_t* block = (block_header_t*)mem;
    block->size   = payload_size;
    block->next   = NULL;
    block->in_use = 0;
    return block;
}

// Insere `block` na free_list, mantendo a ordem por ENDEREÇO
// crescente, e funde com os vizinhos imediatos (anterior e/ou
// próximo) se eles forem fisicamente ADJACENTES na memória — reduz
// fragmentação ao longo do tempo, especialmente em padrões de uso
// que alocam e liberam blocos de tamanhos parecidos repetidamente.
static void insert_and_coalesce(block_header_t* block) {
    block->in_use = 0;

    if (!free_list_head || block < free_list_head) {
        // Vira a nova cabeça da lista.
        block->next = free_list_head;
        free_list_head = block;
    } else {
        block_header_t* prev = free_list_head;
        while (prev->next && prev->next < block) {
            prev = prev->next;
        }
        block->next = prev->next;
        prev->next  = block;
    }

    // Funde com o PRÓXIMO, se adjacente: o fim de `block` (header +
    // payload) bate exatamente com o início do próximo bloco livre.
    if (block->next) {
        uint8_t* end_of_block = (uint8_t*)block + sizeof(block_header_t) + block->size;
        if (end_of_block == (uint8_t*)block->next) {
            block->size += sizeof(block_header_t) + block->next->size;
            block->next = block->next->next;
        }
    }

    // Funde com o ANTERIOR, se adjacente: mesmo teste, na direção
    // oposta. Precisa re-percorrer a partir da cabeça porque o
    // "anterior" na lista pode não ser mais o mesmo depois da fusão
    // acima (que pode ter alterado block->next).
    block_header_t* prev = free_list_head;
    while (prev && prev->next != block) prev = prev->next;
    if (prev) {
        uint8_t* end_of_prev = (uint8_t*)prev + sizeof(block_header_t) + prev->size;
        if (end_of_prev == (uint8_t*)block) {
            prev->size += sizeof(block_header_t) + block->size;
            prev->next = block->next;
        }
    }
}

void* malloc(size_t size) {
    if (size == 0) return NULL;

    if (!heap_start) {
        heap_start = sbrk(0); // primeira chamada: registra onde a
                                // heap deste processo começa (não
                                // usado ainda para nada além de
                                // documentação/futura depuração, mas
                                // é o idioma clássico de
                                // inicialização de um alocador sobre
                                // sbrk).
    }

    size_t payload_size = align_up(size, ALIGNMENT);
    if (payload_size < MIN_BLOCK_PAYLOAD) payload_size = MIN_BLOCK_PAYLOAD;

    // ---- first-fit: percorre a free_list procurando o PRIMEIRO
    // bloco grande o suficiente (não o menor — isso seria "best
    // fit", mais lento e não claramente melhor na prática para um
    // alocador desta simplicidade). ----
    block_header_t* prev = NULL;
    block_header_t* curr = free_list_head;
    while (curr) {
        if (curr->size >= payload_size) {
            // Bloco encontrado. Se ele for grande o suficiente para
            // valer a pena DIVIDIR (sobra espaço para outro bloco
            // útil depois de satisfazer este pedido), divide; senão,
            // entrega o bloco inteiro mesmo com alguma sobra
            // (fragmentação interna pequena é aceitável, dividir só
            // para deixar migalhas inutilizáveis não vale a pena).
            size_t remaining = curr->size - payload_size;
            if (remaining >= sizeof(block_header_t) + MIN_BLOCK_PAYLOAD) {
                uint8_t* split_addr = (uint8_t*)curr + sizeof(block_header_t) + payload_size;
                block_header_t* split = (block_header_t*)split_addr;
                split->size   = remaining - sizeof(block_header_t);
                split->in_use = 0;
                split->next   = curr->next;

                curr->size = payload_size;
                if (prev) prev->next = split;
                else      free_list_head = split;
            } else {
                if (prev) prev->next = curr->next;
                else      free_list_head = curr->next;
            }

            curr->in_use = 1;
            curr->next   = NULL;
            return (void*)(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    // Nenhum bloco livre grande o suficiente — pede mais memória ao
    // kernel via sbrk().
    block_header_t* fresh = request_from_kernel(payload_size);
    if (!fresh) return NULL;
    fresh->in_use = 1;
    return (void*)(fresh + 1);
}

void free(void* ptr) {
    if (!ptr) return; // free(NULL) é sempre um no-op válido em C
    block_header_t* block = (block_header_t*)ptr - 1;
    insert_and_coalesce(block);
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) { free(ptr); return NULL; }

    block_header_t* block = (block_header_t*)ptr - 1;
    if (block->size >= new_size) {
        return ptr; // já cabe no bloco atual — não faz nada mais
                     // sofisticado como devolver o excesso; ver
                     // comentário em stdlib.h sobre essa simplificação.
    }

    void* new_ptr = malloc(new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, block->size); // copia só o que existia antes
    free(ptr);
    return new_ptr;
}
