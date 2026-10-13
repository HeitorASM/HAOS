#include "stdlib.h"
#include "unistd.h"

_Noreturn void exit(int status) {
    // Nesta versão da libc, exit() e _exit() são idênticas — não há
    // ainda handlers de atexit() nem buffers de stdio para
    // descarregar antes de sair (ver comentário em stdlib.h). Quando
    // esses conceitos existirem, é AQUI que a lógica extra entra,
    // sem exigir que programas já escritos contra exit() mudem nada.
    _exit(status);
}
