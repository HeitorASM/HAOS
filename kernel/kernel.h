#pragma once
#include <stdint.h>
#include <stddef.h>

void kernel_main(uint32_t magic, uint32_t mb2_info);

/* Simple memory utilities (no libc) */
void  kmemset(void* dst, uint8_t val, size_t n);
void  kmemcpy(void* dst, const void* src, size_t n);
int   kstrcmp(const char* a, const char* b);
size_t kstrlen(const char* s);
char* kitoa(int val, char* buf, int base);
