#include "unistd.h"
#include "string.h"
#include "haos/syscall.h"

int stat(const char* path, struct haos_stat* st) {
    if (!path || !st) return -1;
    return (int)haos_syscall(SYS_STAT, (long)path, (long)strlen(path), (long)st, 0, 0);
}

int readdir(const char* path, unsigned long index, struct haos_dirent* out) {
    if (!path || !out) return -1;
    return (int)haos_syscall(SYS_READDIR, (long)path, (long)strlen(path),
                             (long)index, (long)out, 0);
}

char* getcwd(char* buf, size_t size) {
    if (!buf || size == 0) return NULL;
    long r = haos_syscall(SYS_GETCWD, (long)buf, (long)size, 0, 0, 0);
    return r < 0 ? NULL : buf;
}

int chdir(const char* path) {
    if (!path) return -1;
    return (int)haos_syscall(SYS_CHDIR, (long)path, (long)strlen(path), 0, 0, 0);
}

int mkdir(const char* path) {
    if (!path) return -1;
    return (int)haos_syscall(SYS_MKDIR, (long)path, (long)strlen(path), 0, 0, 0);
}

int unlink(const char* path) {
    if (!path) return -1;
    return (int)haos_syscall(SYS_UNLINK, (long)path, (long)strlen(path), 0, 0, 0);
}

long lseek(int fd, long offset, int whence) {
    if (fd < 0) return -1;
    return haos_syscall(SYS_LSEEK, fd, offset, whence, 0, 0);
}
