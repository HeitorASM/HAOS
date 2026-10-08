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
    BlockDevice* device;
    HaosSuperblock super;
    uint8_t* bitmap;
    HaosInode* inodes;
} HaosSaveContext;

static uint64_t*    g_hashes;       // 0 = desconhecido
static BlockDevice* g_hash_device;

static uint64_t block_hash(const void* buffer) {
    const uint8_t* p = (const uint8_t*)buffer;
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < HAOSFS_BLOCK_SIZE; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h ? h : 1;
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

static bool layout_for(BlockDevice* device, HaosSuperblock* super) {
    if (!device || !device->ready || device->sector_count <= HAOSFS_START_LBA)
        return false;

    uint64_t available = device->sector_count - HAOSFS_START_LBA;
    uint32_t total_blocks = (uint32_t)(available / HAOSFS_SECTORS_PER_BLOCK);
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
    return (uint64_t)super->total_blocks * HAOSFS_SECTORS_PER_BLOCK <=
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

static bool write_file_data(HaosSaveContext* context, VfsNode* node,
                            HaosInode* inode) {
    uint8_t* block_buffer = (uint8_t*)kzalloc(HAOSFS_BLOCK_SIZE);
    if (!block_buffer) return false;

    uint32_t remaining = node->size;
    uint32_t offset = 0;
    bool ok = true;
    for (uint32_t extent = 0; ok && extent < inode->extent_count; extent++) {
        uint32_t start = inode->extents[extent][0];
        uint32_t count = inode->extents[extent][1];
        for (uint32_t block = 0; ok && block < count && remaining; block++) {
            kmemset(block_buffer, 0, HAOSFS_BLOCK_SIZE);
            uint32_t chunk = remaining < HAOSFS_BLOCK_SIZE ? remaining : HAOSFS_BLOCK_SIZE;
            kmemcpy(block_buffer, node->data + offset, chunk);
            ok = block_write(context->device, start + block, block_buffer);
            offset += chunk;
            remaining -= chunk;
        }
    }
    kfree(block_buffer);
    return ok && remaining == 0;
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
        if (!write_file_data(context, node, inode)) return false;
    }

    for (uint32_t child = 0; child < node->child_count; child++) {
        if (!save_node(context, node->children[child], inode_id, next_inode))
            return false;
    }
    return true;
}

bool haosfs_save(BlockDevice* device, VfsNode* root) {
    if (!device || !root || haosfs_probe(device) != HAOSFS_VALID) return false;

    uint8_t super_block[HAOSFS_BLOCK_SIZE];
    if (!block_read(device, 0, super_block)) return false;

    HaosSaveContext context;
    kmemcpy(&context.super, super_block, sizeof(context.super));
    context.device = device;
    uint32_t bitmap_bytes = context.super.bitmap_blocks * HAOSFS_BLOCK_SIZE;
    context.bitmap = (uint8_t*)kzalloc(bitmap_bytes);
    context.inodes = (HaosInode*)kzalloc(context.super.inode_blocks * HAOSFS_BLOCK_SIZE);
    if (!context.bitmap || !context.inodes) {
        if (context.bitmap) kfree(context.bitmap);
        if (context.inodes) kfree(context.inodes);
        return false;
    }

    for (uint32_t block = 0; block < context.super.data_start; block++)
        bitmap_set(context.bitmap, block);

    uint32_t next_inode = 0;
    bool ok = save_node(&context, root, 0, &next_inode);
    for (uint32_t block = 0; ok && block < context.super.bitmap_blocks; block++)
        ok = block_write(device, context.super.bitmap_start + block,
                         context.bitmap + block * HAOSFS_BLOCK_SIZE);
    for (uint32_t block = 0; ok && block < context.super.inode_blocks; block++)
        ok = block_write(device, context.super.inode_start + block,
                         (uint8_t*)context.inodes + block * HAOSFS_BLOCK_SIZE);

    // Dados/bitmap/inodes precisam estar NA MÍDIA antes do superbloco: o
    // superbloco novo (generation+1) é o que "confirma" a gravação.
    if (ok) ok = block_flush(device);
    context.super.generation++;
    kmemset(super_block, 0, sizeof(super_block));
    kmemcpy(super_block, &context.super, sizeof(context.super));
    if (ok) ok = block_write(device, 0, super_block);
    if (ok) ok = block_flush(device);
    kfree(context.bitmap);
    kfree(context.inodes);
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

bool haosfs_mount(BlockDevice* device, VfsNode* root) {
    if (!device || !root || haosfs_probe(device) != HAOSFS_VALID) return false;

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