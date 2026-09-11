#pragma once

#include "vfs.h"
#include "../drivers/block.h"

typedef enum {
    HAOSFS_EMPTY = 0,
    HAOSFS_VALID,
    HAOSFS_INVALID,
} HaosFsProbe;

HaosFsProbe haosfs_probe(BlockDevice* device);
bool        haosfs_format(BlockDevice* device);
bool        haosfs_mount(BlockDevice* device, VfsNode* root);
bool        haosfs_save(BlockDevice* device, VfsNode* root);