#include "unistd.h"
#include "haos/syscall.h"

void* sbrk(long increment) {
    long ret = haos_syscall(SYS_SBRK, increment, 0, 0, 0, 0);
    return (void*)ret;
}
