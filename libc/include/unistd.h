#pragma once
#include "haos/types.h"

// ============================================================
//  unistd.h — Interface POSIX-like sobre as syscalls do HAOS
//
//  Nomes e assinaturas seguem a convenção POSIX/Linux onde possível,
//  para que quem já programa em C se sinta em terreno familiar — por
//  baixo, cada uma dessas funções é só um wrapper fino sobre
//  haos_syscall() (ver haos/syscall.h). Hoje o HAOS só tem duas
//  syscalls reais (SYS_WRITE, SYS_EXIT); à medida que o kernel
//  ganhar mais (open/read/close de arquivo, etc.), os wrappers
//  correspondentes entram aqui.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Escreve até `count` bytes de `buf` no canal de saída do processo
// (hoje sempre redirecionado para o terminal que o lançou — ver
// task_stdout_write no kernel). Retorna quantos bytes foram
// escritos, ou -1 em caso de erro (ex.: ponteiro de usuário inválido).
// Assinatura igual a write(2) do POSIX, exceto pelo fd: o HAOS ainda
// não tem descritores de arquivo — todo write() vai para o stdout
// implícito do processo. Quando o kernel ganhar múltiplos canais
// (stdout separado de stderr, arquivos abertos), o primeiro
// parâmetro aqui muda para um `int fd` de verdade.
ssize_t write(const void* buf, size_t count);

// Cresce (increment > 0) ou encolhe (increment < 0) a heap do
// processo em `increment` bytes. Retorna o endereço do topo
// ANTERIOR da heap (convenção clássica de sbrk() Unix — some esse
// valor de volta se quiser saber onde o novo bloco começa), ou
// (void*)-1 em erro. sbrk(0) só consulta o topo atual sem mudar
// nada — é assim que malloc() (ver stdlib.h) descobre onde a heap
// está na primeira chamada.
//
// Uso direto por programas é raro — normalmente só malloc()/free()
// chamam isto internamente. Exposto aqui porque é, tecnicamente, uma
// chamada de sistema como outra qualquer.
void* sbrk(long increment);

// Abre um arquivo do VFS para LEITURA (não há suporte a escrita via
// syscall nesta fase). `path` deve ser um caminho ABSOLUTO — não há
// conceito de "diretório de trabalho" de processo ainda. Retorna um
// descritor de arquivo (>= 0) ou -1 em erro (arquivo não encontrado,
// é um diretório, ou a tabela de arquivos abertos deste processo já
// está cheia).
int open(const char* path);

// Lê até `count` bytes do arquivo `fd` para `buf`, avançando a
// posição de leitura. Retorna quantos bytes foram lidos de fato (0
// significa fim de arquivo), ou -1 em erro (fd inválido, ponteiro
// de usuário inválido).
ssize_t read(int fd, void* buf, size_t count);

// Fecha um descritor de arquivo aberto por open(). Retorna 0 em
// sucesso, -1 em erro (fd já fechado ou nunca aberto).
int close(int fd);

// Encerra o processo imediatamente com o código de saída dado.
// Nunca retorna. Equivalente a _exit(2) do POSIX (não roda
// destrutores nem at-exit handlers — o HAOS não tem esse conceito
// ainda; ver stdlib.h para uma futura exit() com essa semântica).
#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void _exit(int status);

#ifdef __cplusplus
}
#endif
