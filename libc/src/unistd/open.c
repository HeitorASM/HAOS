#include "unistd.h"
#include "string.h"
#include "haos/syscall.h"

int open(const char* path) {
    long len = (long)strlen(path);
    long ret = haos_syscall(SYS_OPEN, (long)path, len, 0, 0, 0);
    return (int)ret;
}
