#include "string.h"

void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    unsigned char v = (unsigned char)value;
    for (size_t i = 0; i < n; i++) d[i] = v;
    return dst;
}
