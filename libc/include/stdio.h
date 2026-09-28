#pragma once
#include "haos/types.h"
#include <stdarg.h>

// ============================================================
//  stdio.h — E/S formatada
//
//  printf()/puts() escrevem sempre no stdout do processo (via
//  write(), ver unistd.h) — não há conceito de múltiplos streams
//  (stdout vs stderr) nem de arquivos abertos como FILE* nesta
//  fase; um "arquivo" só existe através de open()/read()/close()
//  (ver unistd.h), que devolvem um descritor inteiro cru, não um
//  FILE* com buffer. fprintf/fopen/fclose ficam para uma iteração
//  futura se um programa realmente precisar de E/S formatada para
//  arquivo, não só para a tela.
//
//  Especificadores suportados por printf/vprintf/snprintf/vsnprintf:
//    %d, %i   — inteiro com sinal (int)
//    %u       — inteiro sem sinal (unsigned int)
//    %x, %X   — hexadecimal sem sinal, minúsculo/maiúsculo
//    %s       — string (const char*)
//    %c       — caractere (int, promovido de char)
//    %p       — ponteiro (impresso como 0x + hexadecimal)
//    %%       — o caractere '%' literal
//    largura e zero-padding: %5d, %05d, %8x etc. (só para os
//      formatos numéricos acima — sem suporte a precisão de string
//      "%.5s" nem a flags como '+' ou '-' de alinhamento à
//      esquerda, que ficam para se algum dia forem necessárias)
//
//  NÃO suportado (produz saída incorreta/truncada se usado):
//    %f, %g, %e (ponto flutuante — HAOS não usa FPU/SSE em
//      processos de usuário ainda, ver -mno-sse no Makefile da
//      libc; mesmo que usasse, um formatador de float correto é
//      trabalho substancial à parte)
//    %ld, %lld, %z (modificadores de tamanho — todo %d/%u/%x aqui
//      assume 32 bits; para imprimir um uint64_t, converta para
//      string manualmente ou trate como caso futuro)
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Escreve `s` seguido de uma quebra de linha no stdout do processo.
// Diferente do puts() do C padrão, não checa erro de stream (não há
// stream aqui, só a syscall write() por baixo) — sempre "funciona"
// do ponto de vista do chamador, mesmo que write() falhe
// silenciosamente.
int puts(const char* s);

// Formata `fmt` com os argumentos variádicos e escreve o resultado
// no stdout do processo. Retorna quantos bytes FORAM ESCRITOS DE
// FATO (pode ser menos que o comprimento total formatado, se a
// formatação exceder o buffer interno — ver PRINTF_MAX_LEN em
// stdio.c; mensagens muito longas são truncadas, não é um erro).
int printf(const char* fmt, ...);

// Versão de printf() que recebe os argumentos já como va_list —
// usada internamente por printf() e útil para quem quiser construir
// seus próprios wrappers variádicos por cima (ex.: uma função de log
// que sempre prefixa a mensagem com um timestamp).
int vprintf(const char* fmt, va_list args);

// Formata `fmt` com os argumentos variádicos para dentro de `buf`
// (até `size` bytes, incluindo o terminador '\0' — comportamento
// idêntico ao snprintf() do C padrão). Retorna quantos bytes TERIAM
// sido escritos se o buffer fosse grande o suficiente (convenção
// POSIX/C99: permite ao chamador detectar truncamento comparando o
// retorno com `size`), não quantos foram de fato copiados.
int snprintf(char* buf, size_t size, const char* fmt, ...);

// Versão de snprintf() que recebe os argumentos já como va_list.
int vsnprintf(char* buf, size_t size, const char* fmt, va_list args);

// ---- scanf: leitura formatada de stdin ----
//
// scanf()/vscanf() leem SEMPRE do stdin do processo (fd 0 — ver
// read() em unistd.h), que é BLOQUEANTE: se não há tecla digitada
// ainda, a chamada dorme até o usuário digitar algo (ver
// sys_read_stdin em kernel/syscall.c). Não existe um FILE* aqui,
// então não há fscanf/sscanf ainda — sscanf() seria o mais fácil de
// adicionar depois (opera sobre uma string já em memória, sem
// syscall nenhuma), mas não existe por enquanto porque nenhum
// programa precisou dele até agora.
//
// Especificadores suportados por scanf/vscanf:
//   %d, %i  — inteiro com sinal, para um `int*`. Aceita sinal
//             opcional (+/-) antes dos dígitos.
//   %u      — inteiro sem sinal, para um `unsigned int*`.
//   %c      — UM caractere, para um `char*`. Diferente de %d/%s,
//             NÃO pula espaços em branco antes de ler (comportamento
//             igual ao scanf() padrão do C — se você quer pular
//             espaço antes de um %c, ponha um espaço literal no
//             formato antes dele, ex.: " %c").
//   %s      — uma "palavra" (sequência sem espaço), para um `char*`
//             com espaço suficiente. Pula espaço em branco antes,
//             para no primeiro espaço/'\n' depois — IGUAL AO scanf()
//             PADRÃO, %s AQUI NÃO TEM LIMITE DE TAMANHO EMBUTIDO
//             (não existe um jeito de expressar "%10s" nesta versão
//             mínima) — o chamador é responsável por garantir um
//             buffer grande o bastante, exatamente o mesmo cuidado
//             que scanf("%s", ...) sempre exigiu em C.
//   espaço em branco no formato (' ', '\t', '\n') — casa com ZERO OU
//             MAIS espaços em branco na entrada (mesmo comportamento
//             do scanf padrão).
//   qualquer outro caractere no formato — deve casar EXATAMENTE com
//             o próximo caractere da entrada, ou o parsing para ali.
//
// NÃO suportado (mesmo motivo de printf — ver comentário acima no
// arquivo): %f/%g/%e (sem FPU), modificadores de tamanho (%ld),
// largura máxima em %s/%d (%10s, %5d).
//
// Retorna quantos especificadores foram preenchidos com sucesso
// (convenção C: pode ser menos que o total se a entrada não bater
// com o formato em algum ponto — o chamador DEVE checar isto antes
// de usar as variáveis, igual a scanf() de verdade), ou -1 se stdin
// fechou (EOF) antes de conseguir ler o primeiro item.
int scanf(const char* fmt, ...);
int vscanf(const char* fmt, va_list args);

#ifdef __cplusplus
}
#endif
