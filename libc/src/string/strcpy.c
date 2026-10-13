#include "string.h"

char* strcpy(char* dst, const char* src) {
    size_t i = 0;
    while ((dst[i] = src[i]) != '\0') i++;
    return dst;
}
