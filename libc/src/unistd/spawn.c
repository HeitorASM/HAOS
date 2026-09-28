#include "unistd.h"
#include "string.h"
#include "haos/syscall.h"

// Serializa argv (array de char*, terminado em NULL) no formato que
// o kernel espera: tokens consecutivos, cada um terminado em '\0'.
#define SPAWN_ARGV_MAX 256

int spawn(const char* path, char* const argv[]) {
    char buf[SPAWN_ARGV_MAX];
    long blen = 0;

    if (argv) {
        for (int i = 0; argv[i] != NULL; i++) {
            long n = (long)strlen(argv[i]) + 1; // inclui o '\0'
            if (blen + n > SPAWN_ARGV_MAX) return -1; // argv grande demais
            memcpy(buf + blen, argv[i], (size_t)n);
            blen += n;
        }
    }

    long ret = haos_syscall(SYS_SPAWN, (long)path, (long)strlen(path),
                            (long)buf, blen, 0);
    return ret == 1 ? 0 : -1;
}
