#include "string.h"

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    // Copia até n bytes de src, incluindo o terminador se couber.
    for (; i < n && src[i] != '\0'; i++) dst[i] = src[i];
    // Preenche o resto com '\0' até completar n — comportamento
    // padrão de strncpy (POSIX), mesmo que pouco intuitivo: se src
    // for >= n bytes, dst NÃO fica terminado em '\0' automaticamente.
    // Quem chama precisa garantir isso manualmente quando necessário.
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}
