#include "string.h"

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        // Destino vem antes da origem na memória: copiar do início
        // para o fim é seguro (nunca sobrescrevemos um byte antes
        // de lê-lo).
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        // Destino vem depois: copiar de trás para frente evita
        // sobrescrever bytes da origem que ainda não foram lidos.
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}
