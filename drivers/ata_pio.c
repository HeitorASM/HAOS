#include "ata_pio.h"
#include "../kernel/types.h"

#define ATA_DATA         0x1F0
#define ATA_ERROR        0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE        0x1F6
#define ATA_STATUS       0x1F7
#define ATA_COMMAND      0x1F7
#define ATA_CONTROL      0x3F6

#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_IDENTIFY  0xEC

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_BSY 0x80

#define ATA_LBA28_MAX 0x10000000ULL
#define ATA_TIMEOUT_TICKS 50000000ULL

static BlockDevice g_device;
static bool g_available;

static uint8_t ata_status(void) {
    return inb(ATA_STATUS);
}

static void ata_select(uint32_t lba) {
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    io_wait();
}

static BlockStatus ata_wait_not_busy(void) {
    uint64_t deadline = rdtsc() + ATA_TIMEOUT_TICKS;
    uint8_t status;

    do {
        status = ata_status();
        if (!(status & ATA_STATUS_BSY)) return BLOCK_OK;
    } while (rdtsc() < deadline);

    return BLOCK_ERR_TIMEOUT;
}

static BlockStatus ata_wait_data(void) {
    uint64_t deadline = rdtsc() + ATA_TIMEOUT_TICKS;
    uint8_t status;

    do {
        status = ata_status();
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return BLOCK_ERR_IO;
        if ((status & ATA_STATUS_DRQ) && !(status & ATA_STATUS_BSY))
            return BLOCK_OK;
    } while (rdtsc() < deadline);

    return BLOCK_ERR_TIMEOUT;
}

static bool ata_range_valid(BlockDevice* device, uint64_t lba, uint32_t count) {
    if (!device || !count || lba >= device->sector_count)
        return false;
    if (lba >= ATA_LBA28_MAX) return false;
    return (uint64_t)count <= device->sector_count - lba &&
           (uint64_t)count <= ATA_LBA28_MAX - lba;
}

static BlockStatus ata_transfer(BlockDevice* device, uint64_t lba,
                                uint32_t count, void* buffer, bool write) {
    if (!device || !device->ready || !buffer ||
        !ata_range_valid(device, lba, count))
        return BLOCK_ERR_INVALID;

    uint16_t* words = (uint16_t*)buffer;
    for (uint32_t sector = 0; sector < count; sector++) {
        uint32_t current_lba = (uint32_t)(lba + sector);
        BlockStatus status = ata_wait_not_busy();
        if (status != BLOCK_OK) return status;

        ata_select(current_lba);
        outb(ATA_SECTOR_COUNT, 1);
        outb(ATA_LBA_LOW,  (uint8_t)(current_lba));
        outb(ATA_LBA_MID,  (uint8_t)(current_lba >> 8));
        outb(ATA_LBA_HIGH, (uint8_t)(current_lba >> 16));
        outb(ATA_COMMAND, write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

        status = ata_wait_data();
        if (status != BLOCK_OK) return status;

        if (write) {
            for (uint32_t word = 0; word < BLOCK_SECTOR_SIZE / 2; word++)
                outw(ATA_DATA, words[word]);
            status = ata_wait_not_busy();
            if (status != BLOCK_OK) return status;
            if (ata_status() & (ATA_STATUS_ERR | ATA_STATUS_DF))
                return BLOCK_ERR_IO;
        } else {
            for (uint32_t word = 0; word < BLOCK_SECTOR_SIZE / 2; word++)
                words[word] = inw(ATA_DATA);
        }

        words += BLOCK_SECTOR_SIZE / 2;
    }

    return BLOCK_OK;
}

static BlockStatus ata_read(BlockDevice* device, uint64_t lba,
                            uint32_t count, void* buffer) {
    return ata_transfer(device, lba, count, buffer, false);
}

static BlockStatus ata_write(BlockDevice* device, uint64_t lba,
                             uint32_t count, const void* buffer) {
    return ata_transfer(device, lba, count, (void*)buffer, true);
}

bool ata_pio_init(void) {
    g_available = false;
    g_device.ready = false;
    g_device.sector_size = BLOCK_SECTOR_SIZE;
    g_device.sector_count = 0;
    g_device.read = ata_read;
    g_device.write = ata_write;
    g_device.driver_data = NULL;

    outb(ATA_CONTROL, 0x02);
    ata_select(0);
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    if (ata_status() == 0) return false;
    if (ata_wait_data() != BLOCK_OK) return false;

    uint16_t identify[256];
    for (uint32_t word = 0; word < 256; word++)
        identify[word] = inw(ATA_DATA);

    if (!(identify[49] & (1 << 9))) return false;

    g_device.sector_count = ((uint64_t)identify[61] << 16) | identify[60];
    if (!g_device.sector_count) return false;
    if (g_device.sector_count > ATA_LBA28_MAX)
        g_device.sector_count = ATA_LBA28_MAX;

    g_device.ready = true;
    g_available = true;
    return true;
}

bool ata_pio_available(void) {
    return g_available;
}

BlockDevice* ata_pio_device(void) {
    return g_available ? &g_device : NULL;
}