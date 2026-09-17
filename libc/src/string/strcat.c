#include "string.h"

char* strcat(char* dst, const char* src) {
    size_t dst_len = strlen(dst);
    size_t i = 0;
    while ((dst[dst_len + i] = src[i]) != '\0') i++;
    return dst;
}
