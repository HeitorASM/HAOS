#include "unistd.h"
#include "haos/syscall.h"

ssize_t write_fd(int fd, const void* buf, size_t count) {
    long ret = haos_syscall(SYS_WRITE_FD, fd, (long)buf, (long)count, 0, 0);
    return (ssize_t)ret;
}
