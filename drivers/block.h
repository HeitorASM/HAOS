#pragma once

#include "../kernel/types.h"

#define BLOCK_SECTOR_SIZE 512U

typedef enum {
    BLOCK_OK = 0,
    BLOCK_ERR_NOT_READY,
    BLOCK_ERR_INVALID,
    BLOCK_ERR_IO,
    BLOCK_ERR_TIMEOUT,
} BlockStatus;

typedef struct BlockDevice BlockDevice;

typedef BlockStatus (*BlockReadFn)(BlockDevice* device, uint64_t lba,
                                   uint32_t count, void* buffer);
typedef BlockStatus (*BlockWriteFn)(BlockDevice* device, uint64_t lba,
                                    uint32_t count, const void* buffer);

struct BlockDevice {
    uint32_t sector_size;
    uint64_t sector_count;
    bool ready;
    BlockReadFn read;
    BlockWriteFn write;
    void* driver_data;
};