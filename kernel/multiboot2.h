#pragma once
#include <stdint.h>

#define MB2_MAGIC_BOOTLOADER 0x36D76289

#define MB2_TAG_END           0
#define MB2_TAG_CMDLINE       1
#define MB2_TAG_BOOTLOADER    2
#define MB2_TAG_MMAP          6
#define MB2_TAG_FRAMEBUFFER   8

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) mb2_tag_framebuffer_t;
