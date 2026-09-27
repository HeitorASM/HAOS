#include "unistd.h"
#include "haos/syscall.h"

int close(int fd) {
    long ret = haos_syscall(SYS_CLOSE, fd, 0, 0, 0, 0);
    return (int)ret;
}
