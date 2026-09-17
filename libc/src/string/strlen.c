#include "string.h"

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') n++;
    return n;
}
