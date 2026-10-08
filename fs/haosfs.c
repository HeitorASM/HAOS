#include "haosfs.h"
#include "../kernel/memory.h"

#define HAOSFS_MAGIC            0x534F4148U
#define HAOSFS_VERSION          1U
#define HAOSFS_START_LBA        2048ULL
#define HAOSFS_BLOCK_SIZE       4096U
#define HAOSFS_SECTORS_PER_BLOCK 8U
#define HAOSFS_INODE_COUNT      1024U
#define HAOSFS_INODE_SIZE       256U
#define HAOSFS_INODE_BLOCKS     ((HAOSFS_INODE_COUNT * HAOSFS_INODE_SIZE) / HAOSFS_BLOCK_SIZE)
#define HAOSFS_MAX_EXTENTS      8U
// Um volume NOVO nunca passa de 1 GiB: o bitmap cabe na heap (32 KiB) e cada
// sync não varre um disco de terabytes. Discos já formatados mantêm o
// tamanho gravado no superbloco.
#define HAOSFS_MAX_BLOCKS       262144U

// Cache de "o que está no disco": hash 64 bits (FNV-1a) dos primeiros
// HAOSFS_HASH_BLOCKS blocos, preenchido ao LER e ao GRAVAR. haosfs_save()
// pula blocos cujo conteúdo não mudou — antes cada sync regravava o disco
// todo (inclusive os 256 KiB da tabela de inodes), e era isso que travava.
#define HAOSFS_HASH_BLOCKS      16384U

// Journal (redo log) no fim do volume, logo depois de total_blocks:
//   [bloco 0] cabeçalho  [blocos 1..N] conteúdo novo dos blocos alterados
// Um sync vira uma transação: (1) escreve o conteúdo no journal + cabeçalho
// e dá FLUSH — esse é o ponto de confirmação; (2) aplica nos lugares de
// verdade e dá FLUSH. Se a energia cair no meio do passo 2, o mount refaz
// a transação a partir do journal; se cair no passo 1, o cabeçalho não
// bate com o hash dos dados e a transação é ignorada (disco = estado
// anterior, intacto). Em ambos os casos o sync é "tudo ou nada".
#define HAOSFS_JOURNAL_BLOCKS       1024U
#define HAOSFS_JOURNAL_MAX_ENTRIES  1016U
#define HAOSFS_JOURNAL_MAGIC        0x4C4E524AU   // "JRNL"
#define FNV_BASIS                   1469598103934665603ULL

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t sectors_per_block;
    uint64_t start_lba;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t bitmap_start;
    uint32_t bitmap_blocks;
    uint32_t inode_start;
    uint32_t inode_blocks;
    uint32_t data_start;
    uint32_t generation;
    uint32_t journal_start;    // 0 e journal_blocks == 0 em discos sem journal
    uint32_t journal_blocks;
} HaosSuperblock;

// Dono/permissões vivem no espaço antes "reservado" do inode — o tamanho
// (256 bytes) e o layout dos campos antigos NÃO mudam, então discos
// formatados antes desta versão continuam montando normalmente. Um inode
// antigo tem esses bytes zerados (perm_magic != HAOSFS_PERM_MAGIC) e
// recebe permissões padrão na montagem (root:root, 0755/0644).
#define HAOSFS_PERM_MAGIC 0x314D5250U   // "PRM1"

typedef struct {
    uint32_t used;
    uint32_t type;
    uint32_t parent;
    uint32_t size;
    uint32_t extent_count;
    uint32_t extents[HAOSFS_MAX_EXTENTS][2];
    char     name[VFS_NAME_MAX];
    uint32_t perm_magic;     // HAOSFS_PERM_MAGIC se uid/gid/mode abaixo são válidos
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t perm_pad;
    uint8_t  reserved[92];
} HaosInode;

_Static_assert(sizeof(HaosInode) == HAOSFS_INODE_SIZE,
               "HaosInode precisa ter exatamente 256 bytes (formato em disco)");

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint64_t seq;
    uint64_t data_hash;      // FNV-1a encadeado do conteúdo de todos os blocos do journal
    uint64_t header_hash;    // FNV-1a deste bloco com este campo = 0 (detecta cabeçalho rasgado)
    uint32_t targets[HAOSFS_JOURNAL_MAX_ENTRIES];
} JournalHeader;

_Static_assert(sizeof(JournalHeader) == HAOSFS_BLOCK_SIZE,
               "JournalHeader precisa ocupar exatamente 1 bloco");

// Uma gravação planejada: `len` bytes de `src` (o resto do bloco = zeros).
typedef struct {
    uint32_t       block;
    const uint8_t* src;
    uint32_t       len;
    uint64_t       hash;
} TxnEntry;

typedef struct {
    TxnEntry* e;
    uint32_t  n, cap;
} Txn;

typedef struct {
    BlockDevice* device;
    HaosSuperblock super;
    uint8_t* bitmap;
    HaosInode* inodes;
    Txn      txn;
    uint8_t* scratch;      // 1 bloco de trabalho
} HaosSaveContext;

static uint64_t*    g_hashes;       // 0 = desconhecido
static BlockDevice* g_hash_device;

static uint64_t fnv_update(uint64_t h, const void* buffer, uint32_t n) {
    const uint8_t* p = (const uint8_t*)buffer;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static uint64_t block_hash(const void* buffer) {
    uint64_t h = fnv_update(FNV_BASIS, buffer, HAOSFS_BLOCK_SIZE);
    return h ? h : 1;
}

static void hash_reset(void) {
    if (g_hashes) kfree(g_hashes);
    g_hashes = NULL;
    g_hash_device = NULL;
}

// Tabela de hashes do dispositivo (zerada ao trocar de dispositivo).
static uint64_t* hashes_for(BlockDevice* device) {
    if (device != g_hash_device) {
        if (g_hashes) kfree(g_hashes);
        g_hashes = (uint64_t*)kzalloc((size_t)HAOSFS_HASH_BLOCKS * sizeof(uint64_t));
        g_hash_device = g_hashes ? device : NULL;
    }
    return g_hashes;
}

static bool block_read(BlockDevice* device, uint32_t block, void* buffer) {
    if (!device || !device->read) return false;
    bool ok = device->read(device,
                           HAOSFS_START_LBA + (uint64_t)block * HAOSFS_SECTORS_PER_BLOCK,
                           HAOSFS_SECTORS_PER_BLOCK, buffer) == BLOCK_OK;
    if (ok && block < HAOSFS_HASH_BLOCKS) {
        uint64_t* t = hashes_for(device);
        if (t) t[block] = block_hash(buffer);
    }
    return ok;
}

static bool block_write(BlockDevice* device, uint32_t block, const void* buffer) {
    if (!device || !device->write) return false;
    uint64_t h = 0;
    uint64_t* t = (block < HAOSFS_HASH_BLOCKS) ? hashes_for(device) : NULL;
    if (t) {
        h = block_hash(buffer);
        if (t[block] == h) return true;          // idêntico ao que já está no disco
        t[block] = 0;                            // em dúvida até a gravação terminar
    }
    bool ok = device->write(device,
                            HAOSFS_START_LBA + (uint64_t)block * HAOSFS_SECTORS_PER_BLOCK,
                            HAOSFS_SECTORS_PER_BLOCK, buffer) == BLOCK_OK;
    if (ok && t) t[block] = h;
    return ok;
}

static bool block_flush(BlockDevice* device) {
    if (!device || !device->flush) return true;  // sem cache de escrita a esvaziar
    return device->flush(device) == BLOCK_OK;
}

// Acesso direto (sem cache de hash): usado no journal e na recuperação.
static bool raw_read(BlockDevice* device, uint32_t block, void* buffer) {
    if (!device || !device->read) return false;
    return device->read(device, HAOSFS_START_LBA + (uint64_t)block * HAOSFS_SECTORS_PER_BLOCK,
                        HAOSFS_SECTORS_PER_BLOCK, buffer) == BLOCK_OK;
}

static bool raw_write(BlockDevice* device, uint32_t block, const void* buffer) {
    if (!device || !device->write) return false;
    return device->write(device, HAOSFS_START_LBA + (uint64_t)block * HAOSFS_SECTORS_PER_BLOCK,
                         HAOSFS_SECTORS_PER_BLOCK, buffer) == BLOCK_OK;
}

static bool layout_for(BlockDevice* device, HaosSuperblock* super) {
    if (!device || !device->ready || device->sector_count <= HAOSFS_START_LBA)
        return false;

    uint64_t available = device->sector_count - HAOSFS_START_LBA;
    uint32_t total_blocks = (uint32_t)(available / HAOSFS_SECTORS_PER_BLOCK);
    uint32_t journal_blocks = 0;
    if (total_blocks > HAOSFS_JOURNAL_BLOCKS + 2048U) {      // disco pequeno demais: sem journal
        total_blocks -= HAOSFS_JOURNAL_BLOCKS;
        journal_blocks = HAOSFS_JOURNAL_BLOCKS;
    }
    if (total_blocks > HAOSFS_MAX_BLOCKS) total_blocks = HAOSFS_MAX_BLOCKS;
    uint32_t bitmap_blocks = (total_blocks + 32767U) / 32768U;

    if (total_blocks <= 1U + bitmap_blocks + HAOSFS_INODE_BLOCKS)
        return false;

    kmemset(super, 0, sizeof(*super));
    super->magic = HAOSFS_MAGIC;
    super->version = HAOSFS_VERSION;
    super->block_size = HAOSFS_BLOCK_SIZE;
    super->sectors_per_block = HAOSFS_SECTORS_PER_BLOCK;
    super->start_lba = HAOSFS_START_LBA;
    super->total_blocks = total_blocks;
    super->inode_count = HAOSFS_INODE_COUNT;
    super->bitmap_start = 1;
    super->bitmap_blocks = bitmap_blocks;
    super->inode_start = super->bitmap_start + bitmap_blocks;
    super->inode_blocks = HAOSFS_INODE_BLOCKS;
    super->data_start = super->inode_start + super->inode_blocks;
    super->generation = 1;
    super->journal_blocks = journal_blocks;
    super->journal_start = journal_blocks ? total_blocks : 0;
    return true;
}

static bool valid_super(const HaosSuperblock* super, BlockDevice* device) {
    if (!super || !device) return false;
    if (super->magic != HAOSFS_MAGIC || super->version != HAOSFS_VERSION)
        return false;
    if (super->block_size != HAOSFS_BLOCK_SIZE ||
        super->sectors_per_block != HAOSFS_SECTORS_PER_BLOCK ||
        super->start_lba != HAOSFS_START_LBA)
        return false;
    if (super->inode_count != HAOSFS_INODE_COUNT ||
        super->inode_blocks != HAOSFS_INODE_BLOCKS)
        return false;
    if (super->data_start >= super->total_blocks)
        return false;
    uint64_t end_blocks = super->total_blocks;
    if (super->journal_blocks) {
        if (super->journal_blocks != HAOSFS_JOURNAL_BLOCKS ||
            super->journal_start != super->total_blocks)
            return false;
        end_blocks += super->journal_blocks;
    }
    return end_blocks * HAOSFS_SECTORS_PER_BLOCK <=
           device->sector_count - HAOSFS_START_LBA;
}

HaosFsProbe haosfs_probe(BlockDevice* device) {
    uint8_t block[HAOSFS_BLOCK_SIZE];
    if (!block_read(device, 0, block)) return HAOSFS_INVALID;

    uint32_t magic = *(uint32_t*)block;
    if (magic == HAOSFS_MAGIC) {
        HaosSuperblock super;
        kmemcpy(&super, block, sizeof(super));
        return valid_super(&super, device) ? HAOSFS_VALID : HAOSFS_INVALID;
    }
    if (magic == 0 || magic == 0xFFFFFFFFU) return HAOSFS_EMPTY;
    return HAOSFS_INVALID;
}

bool haosfs_format(BlockDevice* device) {
    if (haosfs_probe(device) != HAOSFS_EMPTY) return false;

    HaosSuperblock super;
    if (!layout_for(device, &super)) return false;

    uint8_t* zero = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    if (!zero) return false;

    bool ok = true;
    for (uint32_t block = super.bitmap_start;
         ok && block < super.bitmap_start + super.bitmap_blocks; block++)
        ok = block_write(device, block, zero);
    for (uint32_t block = super.inode_start;
         ok && block < super.inode_start + super.inode_blocks; block++)
        ok = block_write(device, block, zero);

    uint8_t super_block[HAOSFS_BLOCK_SIZE];
    kmemset(super_block, 0, sizeof(super_block));
    kmemcpy(super_block, &super, sizeof(super));
    if (ok && super.journal_blocks) ok = raw_write(device, super.journal_start, zero);
    if (ok) ok = block_write(device, 0, super_block);
    kfree(zero);
    return ok;
}

static bool bitmap_test(const uint8_t* bitmap, uint32_t block) {
    return (bitmap[block / 8] & (uint8_t)(1U << (block % 8))) != 0;
}

static void bitmap_set(uint8_t* bitmap, uint32_t block) {
    bitmap[block / 8] |= (uint8_t)(1U << (block % 8));
}

static bool allocate_extents(HaosSaveContext* context, HaosInode* inode,
                             uint32_t blocks) {
    uint32_t remaining = blocks;
    uint32_t search = context->super.data_start;
    inode->extent_count = 0;

    while (remaining) {
        uint32_t run_start = search;
        while (search < context->super.total_blocks &&
               bitmap_test(context->bitmap, search)) search++;
        if (search == context->super.total_blocks) return false;

        run_start = search;
        while (search < context->super.total_blocks &&
               !bitmap_test(context->bitmap, search) &&
               search - run_start < remaining) search++;

        uint32_t run = search - run_start;
        if (inode->extent_count >= HAOSFS_MAX_EXTENTS) return false;
        inode->extents[inode->extent_count][0] = run_start;
        inode->extents[inode->extent_count][1] = run;
        inode->extent_count++;
        for (uint32_t block = run_start; block < search; block++)
            bitmap_set(context->bitmap, block);
        remaining -= run;
    }
    return true;
}

// ---- Transação de sync -------------------------------------------------
static void txn_fill(const TxnEntry* e, uint8_t* out) {
    kmemset(out, 0, HAOSFS_BLOCK_SIZE);
    kmemcpy(out, e->src, e->len);
}

// Planeja gravar `len` bytes de `src` no bloco; PULA se o disco já tem
// exatamente esse conteúdo (cache de hash).
static bool txn_add(HaosSaveContext* c, uint32_t block, const uint8_t* src, uint32_t len) {
    TxnEntry ent;
    ent.block = block; ent.src = src; ent.len = len;
    txn_fill(&ent, c->scratch);
    ent.hash = block_hash(c->scratch);
    if (block < HAOSFS_HASH_BLOCKS) {
        uint64_t* t = hashes_for(c->device);
        if (t && t[block] == ent.hash) return true;
    }
    Txn* tx = &c->txn;
    if (tx->n == tx->cap) {
        uint32_t ncap = tx->cap ? tx->cap * 2 : 256;
        TxnEntry* ne = (TxnEntry*)kmalloc((size_t)ncap * sizeof(TxnEntry));
        if (!ne) return false;
        if (tx->e) { kmemcpy(ne, tx->e, (size_t)tx->n * sizeof(TxnEntry)); kfree(tx->e); }
        tx->e = ne;
        tx->cap = ncap;
    }
    tx->e[tx->n++] = ent;
    return true;
}

static bool txn_commit(HaosSaveContext* c) {
    BlockDevice* device = c->device;
    Txn* tx = &c->txn;
    uint8_t* buf = c->scratch;
    uint64_t* t = hashes_for(device);
    if (t) for (uint32_t i = 0; i < tx->n; i++)
        if (tx->e[i].block < HAOSFS_HASH_BLOCKS) t[tx->e[i].block] = 0;   // em dúvida até terminar

    uint32_t js = c->super.journal_start;
    bool ok = true;

    if (c->super.journal_blocks && tx->n <= HAOSFS_JOURNAL_MAX_ENTRIES) {
        JournalHeader* h = (JournalHeader*)kzalloc(sizeof(JournalHeader));
        if (!h) return false;
        uint64_t dh = FNV_BASIS;
        for (uint32_t i = 0; ok && i < tx->n; i++) {            // 1) conteúdo no journal
            txn_fill(&tx->e[i], buf);
            dh = fnv_update(dh, buf, HAOSFS_BLOCK_SIZE);
            h->targets[i] = tx->e[i].block;
            ok = raw_write(device, js + 1 + i, buf);
        }
        if (ok) {                                               //    + cabeçalho
            h->magic = HAOSFS_JOURNAL_MAGIC;
            h->count = tx->n;
            h->seq = c->super.generation;
            h->data_hash = dh;
            h->header_hash = 0;
            h->header_hash = block_hash(h);
            ok = raw_write(device, js, h);
        }
        kfree(h);
        if (ok) ok = block_flush(device);                       //    CONFIRMADO
        for (uint32_t i = 0; ok && i < tx->n; i++) {            // 2) aplica no lugar
            txn_fill(&tx->e[i], buf);
            ok = raw_write(device, tx->e[i].block, buf);
        }
        if (ok) ok = block_flush(device);
    } else {
        // Sem journal (disco antigo/pequeno) ou transação grande demais:
        // grava no lugar, dados/metadados primeiro e o superbloco por
        // último. Não é atômico — mas invalida antes qualquer cabeçalho
        // antigo, para um replay velho nunca desfazer estas gravações.
        if (c->super.journal_blocks) {
            kmemset(buf, 0, HAOSFS_BLOCK_SIZE);
            ok = raw_write(device, js, buf) && block_flush(device);
        }
        for (uint32_t i = 0; ok && i < tx->n; i++) {
            if (tx->e[i].block == 0) continue;
            txn_fill(&tx->e[i], buf);
            ok = raw_write(device, tx->e[i].block, buf);
        }
        if (ok) ok = block_flush(device);
        for (uint32_t i = 0; ok && i < tx->n; i++) {
            if (tx->e[i].block != 0) continue;
            txn_fill(&tx->e[i], buf);
            ok = raw_write(device, 0, buf);
        }
        if (ok) ok = block_flush(device);
    }

    if (ok && t) for (uint32_t i = 0; i < tx->n; i++)
        if (tx->e[i].block < HAOSFS_HASH_BLOCKS) t[tx->e[i].block] = tx->e[i].hash;
    return ok;
}

// Planeja a gravação do conteúdo de um arquivo nos extents já alocados.
static bool plan_file_data(HaosSaveContext* context, VfsNode* node, HaosInode* inode) {
    uint32_t remaining = node->size;
    uint32_t offset = 0;
    for (uint32_t extent = 0; extent < inode->extent_count; extent++) {
        uint32_t start = inode->extents[extent][0];
        uint32_t count = inode->extents[extent][1];
        for (uint32_t block = 0; block < count && remaining; block++) {
            uint32_t chunk = remaining < HAOSFS_BLOCK_SIZE ? remaining : HAOSFS_BLOCK_SIZE;
            if (!txn_add(context, start + block, (const uint8_t*)node->data + offset, chunk))
                return false;
            offset += chunk;
            remaining -= chunk;
        }
    }
    return remaining == 0;
}

static bool save_node(HaosSaveContext* context, VfsNode* node,
                      uint32_t parent, uint32_t* next_inode) {
    if (!node || *next_inode >= context->super.inode_count) return false;

    uint32_t inode_id = *next_inode;
    (*next_inode)++;
    node->fs_inode = inode_id;
    HaosInode* inode = &context->inodes[inode_id];
    inode->used = 1;
    inode->type = (uint32_t)node->type;
    inode->parent = parent;
    inode->size = node->type == VFS_FILE ? node->size : 0;
    kstrncpy(inode->name, node->name, VFS_NAME_MAX - 1);
    inode->perm_magic = HAOSFS_PERM_MAGIC;
    inode->uid  = node->uid;
    inode->gid  = node->gid;
    inode->mode = node->mode;

    if (node->type == VFS_FILE) {
        uint32_t blocks = (node->size + HAOSFS_BLOCK_SIZE - 1) / HAOSFS_BLOCK_SIZE;
        if (!allocate_extents(context, inode, blocks)) return false;
        if (!plan_file_data(context, node, inode)) return false;
    }

    for (uint32_t child = 0; child < node->child_count; child++) {
        if (!save_node(context, node->children[child], inode_id, next_inode))
            return false;
    }
    return true;
}

bool haosfs_save(BlockDevice* device, VfsNode* root) {
    if (!device || !root || haosfs_probe(device) != HAOSFS_VALID) return false;

    HaosSaveContext context;
    kmemset(&context, 0, sizeof(context));
    context.device = device;
    uint8_t* super_block = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    context.scratch = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    bool ok = super_block && context.scratch && block_read(device, 0, super_block);

    if (ok) {
        kmemcpy(&context.super, super_block, sizeof(context.super));
        context.bitmap = (uint8_t*)kzalloc(context.super.bitmap_blocks * HAOSFS_BLOCK_SIZE);
        context.inodes = (HaosInode*)kzalloc(context.super.inode_blocks * HAOSFS_BLOCK_SIZE);
        ok = context.bitmap && context.inodes;
    }

    if (ok) {
        for (uint32_t block = 0; block < context.super.data_start; block++)
            bitmap_set(context.bitmap, block);

        uint32_t next_inode = 0;
        ok = save_node(&context, root, 0, &next_inode);
        for (uint32_t block = 0; ok && block < context.super.bitmap_blocks; block++)
            ok = txn_add(&context, context.super.bitmap_start + block,
                         context.bitmap + block * HAOSFS_BLOCK_SIZE, HAOSFS_BLOCK_SIZE);
        for (uint32_t block = 0; ok && block < context.super.inode_blocks; block++)
            ok = txn_add(&context, context.super.inode_start + block,
                         (uint8_t*)context.inodes + block * HAOSFS_BLOCK_SIZE, HAOSFS_BLOCK_SIZE);
    }

    // Nada mudou desde o último sync: não grava nada (nem gasta a "generation").
    if (ok && context.txn.n > 0) {
        context.super.generation++;
        kmemset(super_block, 0, HAOSFS_BLOCK_SIZE);
        kmemcpy(super_block, &context.super, sizeof(context.super));
        ok = txn_add(&context, 0, super_block, HAOSFS_BLOCK_SIZE) && txn_commit(&context);
    }

    if (context.bitmap) kfree(context.bitmap);
    if (context.inodes) kfree(context.inodes);
    if (context.txn.e)  kfree(context.txn.e);
    if (context.scratch) kfree(context.scratch);
    if (super_block)    kfree(super_block);
    return ok;
}

static bool read_file_data(BlockDevice* device, const HaosSuperblock* super,
                           const HaosInode* inode, VfsNode* node) {
    if (inode->size > VFS_FILE_MAX - 1) return false;
    if (!vfs_ensure_capacity(node, inode->size + 1)) return false;

    uint8_t* block_buffer = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    if (!block_buffer) return false;
    uint32_t remaining = inode->size;
    uint32_t offset = 0;
    bool ok = true;
    for (uint32_t extent = 0; ok && extent < inode->extent_count; extent++) {
        uint32_t start = inode->extents[extent][0];
        uint32_t count = inode->extents[extent][1];
        if (start < super->data_start || start + count > super->total_blocks) {
            ok = false;
            break;
        }
        for (uint32_t block = 0; ok && block < count && remaining; block++) {
            ok = block_read(device, start + block, block_buffer);
            uint32_t chunk = remaining < HAOSFS_BLOCK_SIZE ? remaining : HAOSFS_BLOCK_SIZE;
            if (ok) kmemcpy(node->data + offset, block_buffer, chunk);
            offset += chunk;
            remaining -= chunk;
        }
    }
    if (ok && remaining == 0) {
        node->size = inode->size;
        node->data[node->size] = 0;
    } else {
        ok = false;
    }
    kfree(block_buffer);
    return ok;
}

static bool valid_inode(const HaosInode* inode, const HaosSuperblock* super) {
    if (!inode || !super || !inode->used) return false;
    if (inode->type != VFS_DIR && inode->type != VFS_FILE) return false;
    if (!inode->name[0] || kstrlen(inode->name) >= VFS_NAME_MAX) return false;
    if (inode->extent_count > HAOSFS_MAX_EXTENTS) return false;
    if (inode->type == VFS_DIR && (inode->size || inode->extent_count)) return false;

    uint64_t capacity = 0;
    for (uint32_t extent = 0; extent < inode->extent_count; extent++) {
        uint32_t start = inode->extents[extent][0];
        uint32_t count = inode->extents[extent][1];
        if (!count || start < super->data_start || start >= super->total_blocks ||
            count > super->total_blocks - start)
            return false;
        capacity += (uint64_t)count * HAOSFS_BLOCK_SIZE;
    }
    return inode->type == VFS_DIR || inode->size <= capacity;
}

// Copia dono/permissões do inode para o nó do VFS. Inode sem perm_magic
// (disco de uma versão anterior) => padrão root:root 0755/0644. Os
// executáveis ".hexec" não precisam de tratamento especial: rodar um
// programa só exige poder LER o arquivo (ver fs/perm.h).
static void apply_inode_perms(VfsNode* node, const HaosInode* inode) {
    if (inode->perm_magic == HAOSFS_PERM_MAGIC) {
        vfs_set_owner(node, inode->uid, inode->gid, inode->mode);
    } else {
        vfs_set_owner(node, 0, 0,
                      node->type == VFS_DIR ? VFS_MODE_DIR_DEFAULT : VFS_MODE_FILE_DEFAULT);
    }
}

static bool blocks_equal(const uint8_t* x, const uint8_t* y) {
    for (uint32_t i = 0; i < HAOSFS_BLOCK_SIZE; i++) if (x[i] != y[i]) return false;
    return true;
}

// Recuperação: se o último sync foi confirmado no journal mas pode não ter
// sido aplicado por inteiro (queda de energia), refaz a transação. É
// idempotente: só regrava blocos que diferem do journal.
static void journal_recover(BlockDevice* device) {
    uint8_t* sb = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    JournalHeader* h = (JournalHeader*)kzalloc(sizeof(JournalHeader));
    uint8_t* a = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    uint8_t* b = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);

    if (sb && h && a && b && raw_read(device, 0, sb)) {
        HaosSuperblock super;
        kmemcpy(&super, sb, sizeof(super));
        if (valid_super(&super, device) && super.journal_blocks &&
            raw_read(device, super.journal_start, h) &&
            h->magic == HAOSFS_JOURNAL_MAGIC &&
            h->count > 0 && h->count <= HAOSFS_JOURNAL_MAX_ENTRIES) {
            uint64_t stored = h->header_hash;
            h->header_hash = 0;
            bool ok = (block_hash(h) == stored);
            uint64_t dh = FNV_BASIS;
            for (uint32_t i = 0; ok && i < h->count; i++) {
                ok = h->targets[i] < super.total_blocks &&
                     raw_read(device, super.journal_start + 1 + i, a);
                if (ok) dh = fnv_update(dh, a, HAOSFS_BLOCK_SIZE);
            }
            if (ok && dh == h->data_hash) {
                bool wrote = false;
                for (uint32_t i = 0; ok && i < h->count; i++) {
                    ok = raw_read(device, super.journal_start + 1 + i, a) &&
                         raw_read(device, h->targets[i], b);
                    if (ok && !blocks_equal(a, b)) {
                        ok = raw_write(device, h->targets[i], a);
                        wrote = true;
                    }
                }
                if (wrote) block_flush(device);
            }
        }
    }
    if (sb) kfree(sb);
    if (h)  kfree(h);
    if (a)  kfree(a);
    if (b)  kfree(b);
    hash_reset();     // o disco pode ter mudado por baixo do cache
}

// Disco formatado antes do journal: cria o journal sem reformatar. Usa o
// espaço livre depois de total_blocks, ou — se o volume ocupa o disco todo —
// toma os últimos blocos, desde que nenhum arquivo esteja lá. Qualquer
// falha só significa "continua sem journal".
static void journal_upgrade(BlockDevice* device) {
    uint8_t* sb = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    if (!sb) return;
    HaosSuperblock super;
    if (!raw_read(device, 0, sb)) { kfree(sb); return; }
    kmemcpy(&super, sb, sizeof(super));
    if (!valid_super(&super, device) || super.journal_blocks) { kfree(sb); return; }

    uint64_t avail = (device->sector_count - HAOSFS_START_LBA) / HAOSFS_SECTORS_PER_BLOCK;
    uint32_t new_total = super.total_blocks;
    bool ok = true;
    if (avail < (uint64_t)super.total_blocks + HAOSFS_JOURNAL_BLOCKS) {
        if (super.total_blocks < super.data_start + HAOSFS_JOURNAL_BLOCKS + 64U) {
            ok = false;
        } else {
            new_total = super.total_blocks - HAOSFS_JOURNAL_BLOCKS;
            HaosInode* inodes = (HaosInode*)kzalloc(super.inode_blocks * HAOSFS_BLOCK_SIZE);
            ok = inodes != NULL;
            for (uint32_t blk = 0; ok && blk < super.inode_blocks; blk++)
                ok = raw_read(device, super.inode_start + blk,
                              (uint8_t*)inodes + blk * HAOSFS_BLOCK_SIZE);
            for (uint32_t id = 0; ok && id < super.inode_count; id++) {
                const HaosInode* in = &inodes[id];
                if (!in->used) continue;
                if (in->extent_count > HAOSFS_MAX_EXTENTS) { ok = false; break; }
                for (uint32_t e = 0; e < in->extent_count; e++)
                    if ((uint64_t)in->extents[e][0] + in->extents[e][1] > new_total) ok = false;
            }
            if (inodes) kfree(inodes);
        }
    }
    if (ok) {
        super.total_blocks = new_total;
        super.journal_start = new_total;
        super.journal_blocks = HAOSFS_JOURNAL_BLOCKS;
        uint8_t* zero = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
        ok = zero && raw_write(device, super.journal_start, zero) && block_flush(device);
        if (zero) kfree(zero);
        if (ok) {                       // só DEPOIS do cabeçalho zerado "anunciamos" o journal
            kmemcpy(sb, &super, sizeof(super));
            ok = raw_write(device, 0, sb) && block_flush(device);
        }
    }
    kfree(sb);
    hash_reset();
}

bool haosfs_mount(BlockDevice* device, VfsNode* root) {
    if (!device || !root || haosfs_probe(device) != HAOSFS_VALID) return false;

    journal_recover(device);     // refaz um sync confirmado e interrompido
    journal_upgrade(device);     // disco antigo: cria o journal

    uint8_t super_block[HAOSFS_BLOCK_SIZE];
    if (!block_read(device, 0, super_block)) return false;
    HaosSuperblock super;
    kmemcpy(&super, super_block, sizeof(super));

    HaosInode* inodes = (HaosInode*)kzalloc(super.inode_blocks * HAOSFS_BLOCK_SIZE);
    VfsNode** nodes = (VfsNode**)kzalloc(super.inode_count * sizeof(VfsNode*));
    if (!inodes || !nodes) {
        if (inodes) kfree(inodes);
        if (nodes) kfree(nodes);
        return false;
    }
    bool ok = true;
    for (uint32_t block = 0; ok && block < super.inode_blocks; block++)
        ok = block_read(device, super.inode_start + block,
                        (uint8_t*)inodes + block * HAOSFS_BLOCK_SIZE);

    // Aquece o cache de hash com o bitmap (o mount não precisa dele, mas o
    // próximo sync compara com ele — sem isto, o 1º sync regravaria o bitmap).
    if (ok) {
        uint8_t* warm = (uint8_t*)kmalloc(HAOSFS_BLOCK_SIZE);
        if (warm) {
            for (uint32_t block = 0; block < super.bitmap_blocks; block++)
                block_read(device, super.bitmap_start + block, warm);
            kfree(warm);
        }
    }

    if (ok && (!valid_inode(&inodes[0], &super) ||
               inodes[0].type != VFS_DIR || kstrcmp(inodes[0].name, "/") != 0))
        ok = false;
    if (ok) {
        root->fs_inode = 0;
        apply_inode_perms(root, &inodes[0]);
        nodes[0] = root;
        for (uint32_t id = 1; id < super.inode_count; id++) {
            HaosInode* inode = &inodes[id];
            if (!inode->used) continue;
            if (!valid_inode(inode, &super)) {
                ok = false;
                break;
            }
            if (inode->parent >= super.inode_count || !nodes[inode->parent]) {
                ok = false;
                break;
            }
            VfsNode* node = inode->type == VFS_DIR
                ? vfs_mkdir(nodes[inode->parent], inode->name)
                : vfs_touch(nodes[inode->parent], inode->name);
            if (!node) {
                ok = false;
                break;
            }
            node->fs_inode = id;
            apply_inode_perms(node, inode);
            nodes[id] = node;
            if (inode->type == VFS_FILE && !read_file_data(device, &super, inode, node)) {
                ok = false;
                break;
            }
        }
    }
    kfree(inodes);
    kfree(nodes);
    return ok;
}