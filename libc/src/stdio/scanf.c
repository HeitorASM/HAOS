#include "stdio.h"
#include "unistd.h"

// ============================================================
//  libc/src/stdio/scanf.c — leitura formatada mínima de stdin
//
//  Sem FILE* bufferizado nesta libc (ver comentário em stdio.h),
//  então lemos direto da syscall read(0, &c, 1) — um byte de cada
//  vez. O problema clássico de qualquer scanf: para saber que um
//  número terminou (ex.: "42x"), é preciso LER o caractere seguinte
//  ('x') e perceber que ele não é mais dígito — mas esse caractere
//  já foi consumido do stdin_ring do kernel, não tem como "devolver"
//  ele pra lá. Resolvido com um pushback de 1 byte aqui na libc
//  (has_pushback/pushback_char, abaixo): scan_getc() sempre checa o
//  pushback primeiro antes de chamar read() de novo.
// ============================================================

static int has_pushback = 0;
static char pushback_char = 0;

// Lê 1 byte do stdin (bloqueante — ver sys_read_stdin no kernel),
// consumindo o pushback primeiro se houver um. Retorna o byte lido,
// ou -1 em EOF (stdin fechado — ver comentário em sys_read_stdin
// sobre quando isso acontece).
static int scan_getc(void) {
    if (has_pushback) {
        has_pushback = 0;
        return (unsigned char)pushback_char;
    }
    char c;
    ssize_t n = read(0, &c, 1);
    if (n <= 0) return -1;
    return (unsigned char)c;
}

// Devolve `c` para ser o PRÓXIMO byte lido por scan_getc() — usado
// quando um %d/%u/%s lê um caractere que não pertence mais ao
// número/palavra atual e precisa "olhar antes" sem consumir de
// verdade. Só suporta 1 nível de pushback (suficiente para este
// parser — nenhum especificador aqui precisa olhar mais de 1
// caractere à frente).
static void scan_ungetc(int c) {
    if (c < 0) return; // não empurra EOF de volta — não há o que "desfazer"
    has_pushback = 1;
    pushback_char = (char)c;
}

static int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

// Pula espaço em branco (zero ou mais) — usado tanto para o
// especificador de espaço explícito no formato quanto internamente
// antes de %d/%u/%s (que sempre pulam espaço primeiro, como o
// scanf() padrão exige).
static void skip_whitespace(void) {
    int c;
    while ((c = scan_getc()) != -1 && is_space(c)) {
        // consome
    }
    scan_ungetc(c); // devolve o primeiro não-espaço (ou EOF) encontrado
}

// ---- %d / %i: inteiro com sinal ----
// Retorna 1 se conseguiu ler pelo menos 1 dígito (grava em *out), 0
// caso a entrada não comece com um inteiro válido (nada é consumido
// de forma irreversível nesse caso — o caractere ofensivo fica no
// pushback para o próximo especificador/chamada tentar de novo).
static int scan_int(int* out) {
    skip_whitespace();
    int c = scan_getc();
    int negative = 0;
    if (c == '+' || c == '-') {
        negative = (c == '-');
        c = scan_getc();
    }
    if (!is_digit(c)) {
        scan_ungetc(c);
        return 0; // nem um dígito — falha de casamento, como scanf real
    }
    long value = 0;
    while (is_digit(c)) {
        value = value * 10 + (c - '0');
        c = scan_getc();
    }
    scan_ungetc(c); // devolve o separador que encerrou o número
    *out = (int)(negative ? -value : value);
    return 1;
}

static int scan_uint(unsigned int* out) {
    skip_whitespace();
    int c = scan_getc();
    if (!is_digit(c)) {
        scan_ungetc(c);
        return 0;
    }
    unsigned long value = 0;
    while (is_digit(c)) {
        value = value * 10 + (unsigned long)(c - '0');
        c = scan_getc();
    }
    scan_ungetc(c);
    *out = (unsigned int)value;
    return 1;
}

// ---- %s: palavra sem espaço ----
// Igual ao scanf("%s", ...) padrão do C: pula espaço antes, lê até
// o próximo espaço/EOF, sempre termina em '\0'. SEM limite de
// tamanho embutido — ver aviso grande em stdio.h sobre isso.
static int scan_word(char* out) {
    skip_whitespace();
    int c = scan_getc();
    if (c == -1) return 0; // EOF antes de qualquer caractere de verdade
    int len = 0;
    while (c != -1 && !is_space(c)) {
        out[len++] = (char)c;
        c = scan_getc();
    }
    scan_ungetc(c);
    out[len] = '\0';
    return len > 0;
}

// ---- %c: um único caractere, SEM pular espaço ----
static int scan_char(char* out) {
    int c = scan_getc();
    if (c == -1) return 0;
    *out = (char)c;
    return 1;
}

int vscanf(const char* fmt, va_list args) {
    int matched = 0;
    int any_input_consumed = 0;

    while (*fmt) {
        char fc = *fmt;

        if (is_space(fc)) {
            // Um ou mais espaços no FORMATO casam com ZERO OU MAIS
            // espaços na ENTRADA — comportamento padrão do scanf.
            skip_whitespace();
            fmt++;
            continue;
        }

        if (fc == '%') {
            fmt++;
            char spec = *fmt;
            if (spec == '\0') break; // '%' no fim do formato — nada a fazer

            int ok = 0;
            switch (spec) {
                case 'd':
                case 'i': {
                    int* p = va_arg(args, int*);
                    ok = scan_int(p);
                    break;
                }
                case 'u': {
                    unsigned int* p = va_arg(args, unsigned int*);
                    ok = scan_uint(p);
                    break;
                }
                case 's': {
                    char* p = va_arg(args, char*);
                    ok = scan_word(p);
                    break;
                }
                case 'c': {
                    char* p = va_arg(args, char*);
                    ok = scan_char(p);
                    break;
                }
                case '%': {
                    // "%%" no formato casa com um '%' literal na
                    // entrada — não conta como argumento preenchido.
                    int c = scan_getc();
                    if (c == '%') { ok = 1; any_input_consumed = 1; }
                    else { scan_ungetc(c); ok = 0; }
                    fmt++;
                    if (!ok) return matched;
                    continue;
                }
                default:
                    // Especificador desconhecido — não há como
                    // continuar de forma segura (não sabemos que
                    // tipo de ponteiro viria a seguir em va_arg).
                    return matched;
            }

            fmt++;
            if (!ok) {
                // EOF logo de cara (antes de qualquer item) devolve
                // -1, seguindo a convenção C de scanf(); se algum
                // item já tinha sido lido com sucesso antes deste,
                // devolve quantos foram (comportamento padrão).
                if (matched == 0 && !any_input_consumed) return -1;
                return matched;
            }
            matched++;
            any_input_consumed = 1;
            continue;
        }

        // Caractere literal no formato: precisa casar EXATAMENTE
        // com o próximo byte da entrada.
        int c = scan_getc();
        if (c != fc) {
            scan_ungetc(c);
            return matched;
        }
        any_input_consumed = 1;
        fmt++;
    }

    return matched;
}

int scanf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vscanf(fmt, args);
    va_end(args);
    return result;
}
