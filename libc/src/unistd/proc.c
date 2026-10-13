#include "unistd.h"
#include "haos/syscall.h"

int getpid(void) {
    return (int)haos_syscall(SYS_GETPID, 0, 0, 0, 0, 0);
}

void yield(void) {
    haos_syscall(SYS_YIELD, 0, 0, 0, 0, 0);
}

unsigned long uptime_ms(void) {
    return (unsigned long)haos_syscall(SYS_UPTIME_MS, 0, 0, 0, 0, 0);
}

int sleep_ms(unsigned long ms) {
    return (int)haos_syscall(SYS_SLEEP_MS, (long)ms, 0, 0, 0, 0);
}

// Identidade do processo (usuários). Nunca é 0 para programas de usuário.
int getuid(void) {
    return (int)haos_syscall(SYS_GETUID, 0, 0, 0, 0, 0);
}

int getgid(void) {
    return (int)haos_syscall(SYS_GETGID, 0, 0, 0, 0, 0);
}

int getrandom(void* buf, unsigned long len) {
    return (int)haos_syscall(SYS_GETRANDOM, (long)buf, (long)len, 0, 0, 0);
}
