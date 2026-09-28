#include "unistd.h"
#include "string.h"
#include "haos/syscall.h"

int open(const char* path) {
    long len = (long)strlen(path);
    long ret = haos_syscall(SYS_OPEN, (long)path, len, 0, 0, 0);
    return (int)ret;
}

int open2(const char* path, int flags) {
    long len = (long)strlen(path);
    long ret = haos_syscall(SYS_OPEN, (long)path, len, (long)flags, 0, 0);
    return (int)ret;
}
