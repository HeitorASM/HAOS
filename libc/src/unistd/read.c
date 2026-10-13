#include "unistd.h"
#include "haos/syscall.h"

ssize_t read(int fd, void* buf, size_t count) {
    long ret = haos_syscall(SYS_READ, fd, (long)buf, (long)count, 0, 0);
    return (ssize_t)ret;
}
