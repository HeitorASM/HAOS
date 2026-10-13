#include "unistd.h"
#include "haos/syscall.h"

int waitpid(int pid, unsigned long* out_exit_code) {
    // sys_waitpid recebe pid como uint64_t sem sinal — um pid
    // negativo (não deveria existir, já que spawn() só devolve
    // valores positivos ou -1, que o chamador já deveria ter tratado
    // antes de chegar aqui) seria reinterpretado como um id gigante
    // que nunca bate com nada, então cai naturalmente no caso "0:
    // pid não encontrado" do kernel — não precisa de checagem extra
    // aqui além de não deixar um -1 explícito passar disfarçado.
    if (pid <= 0) return -1;

    long ret = haos_syscall(SYS_WAITPID, (unsigned long)pid,
                            (long)out_exit_code, 0, 0, 0);
    return (int)ret;
}
