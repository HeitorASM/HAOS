#include "unistd.h"
#include "haos/syscall.h"

_Noreturn void _exit(int status) {
    haos_syscall(SYS_EXIT, (long)status, 0, 0, 0, 0);
    // SYS_EXIT nunca retorna (o kernel troca de contexto para outra
    // task e esta nunca mais roda) — mas o compilador não sabe disso
    // olhando só para a assinatura de haos_syscall (que retorna
    // `long` normalmente). Este loop nunca deveria ser alcançado; ele
    // só existe para satisfazer o atributo _Noreturn caso, por algum
    // bug do kernel, a chamada realmente retornasse.
    for (;;) { }
}
