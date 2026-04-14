#pragma once
#include "types.h"

void   memory_init(void);
void*  kmalloc(size_t n);
void*  kzalloc(size_t n);
void   kfree(void* p);
void*  kmemset(void* dst, int val, size_t n);
void*  kmemcpy(void* dst, const void* src, size_t n);
int    kstrcmp(const char* a, const char* b);
int    kstrncmp(const char* a, const char* b, size_t n);
size_t kstrlen(const char* s);
char*  kstrcpy(char* dst, const char* src);
char*  kstrcat(char* dst, const char* src);
void   kitoa(int64_t n, char* buf);
void   kuitoa_hex(uint64_t n, char* buf);
