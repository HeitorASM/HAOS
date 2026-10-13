#include "stdio.h"
#include "string.h"
#include "unistd.h"

// ============================================================
//  libc/src/stdio/printf.c — núcleo do formatador + wrappers
//
//  Todo o parsing de "%..." mora em UMA função central,
//  format_core(), que não escreve nada diretamente — ela chama um
//  callback (sink_fn) a cada pedaço de texto pronto (uma string
//  literal do formato, ou um número já convertido). Isso evita
//  duplicar a lógica de parsing entre printf() (que manda a saída
//  para write()) e snprintf() (que manda para um buffer em memória)
//  — a ÚNICA diferença entre eles é o que este callback faz com os
//  bytes prontos.
// ============================================================

typedef struct {
    void (*emit)(void* ctx, const char* data, size_t len);
    void* ctx;
    size_t total_written; // conta TUDO que passou pelo sink, mesmo
                            // que o sink real (ex.: snprintf com
                            // buffer pequeno) não tenha copiado tudo
                            // — é assim que vsnprintf calcula o
                            // "retorno C99" (quanto teria sido
                            // escrito num buffer infinito).
} sink_t;

static void sink_emit(sink_t* s, const char* data, size_t len) {
    s->emit(s->ctx, data, len);
    s->total_written += len;
}

// Converte um inteiro para string numa base arbitrária (10 ou 16
// aqui), preenchendo `out` de trás para frente e retornando quantos
// caracteres foram escritos. `uppercase` só importa para base 16.
static int format_uint(uint32_t value, int base, int uppercase, char* out, int out_cap) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char* digits = uppercase ? digits_upper : digits_lower;

    if (value == 0) {
        if (out_cap > 0) out[0] = '0';
        return 1;
    }

    char tmp[32];
    int n = 0;
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = digits[value % (uint32_t)base];
        value /= (uint32_t)base;
    }
    int len = n < out_cap ? n : out_cap;
    for (int i = 0; i < len; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return len;
}

// Parseia e executa UM especificador de formato (o que vem logo
// depois de um '%'), avançando `*fmt_ptr` até depois dele. Emite o
// resultado através de `s`. Retorna o char seguinte a processar (só
// para clareza de fluxo — na prática *fmt_ptr já reflete isso).
static void format_core(sink_t* s, const char* fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') {
            // Trecho literal: emite de uma vez até o próximo '%' ou
            // fim da string, em vez de caractere por caractere — mais
            // eficiente e simplifica a lógica de largura/padding, que
            // só se aplica a conversões, nunca a texto literal.
            const char* start = fmt;
            while (*fmt && *fmt != '%') fmt++;
            sink_emit(s, start, (size_t)(fmt - start));
            continue;
        }

        fmt++; // pula o '%'

        // ---- largura opcional: %5d, %05d, etc. ----
        int zero_pad = 0;
        int width = 0;
        if (*fmt == '0') { zero_pad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        char numbuf[32];
        int numlen = 0;
        int is_negative = 0;

        switch (*fmt) {
            case 'd':
            case 'i': {
                int32_t value = va_arg(args, int32_t);
                if (value < 0) { is_negative = 1; value = -value; }
                numlen = format_uint((uint32_t)value, 10, 0, numbuf, (int)sizeof(numbuf));
                break;
            }
            case 'u': {
                uint32_t value = va_arg(args, uint32_t);
                numlen = format_uint(value, 10, 0, numbuf, (int)sizeof(numbuf));
                break;
            }
            case 'x': {
                uint32_t value = va_arg(args, uint32_t);
                numlen = format_uint(value, 16, 0, numbuf, (int)sizeof(numbuf));
                break;
            }
            case 'X': {
                uint32_t value = va_arg(args, uint32_t);
                numlen = format_uint(value, 16, 1, numbuf, (int)sizeof(numbuf));
                break;
            }
            case 'p': {
                // Ponteiros são impressos como "0x" + hex de 64 bits
                // — diferente de %x (32 bits), já que este é um
                // ambiente de 64 bits e endereços não cabem em
                // uint32_t. Formatado à parte por isso, sem passar
                // por format_uint (que só trabalha com 32 bits).
                uintptr_t value = (uintptr_t)va_arg(args, void*);
                sink_emit(s, "0x", 2);
                static const char hexdigits[] = "0123456789abcdef";
                char buf64[16];
                for (int i = 0; i < 16; i++) {
                    buf64[15 - i] = hexdigits[value & 0xF];
                    value >>= 4;
                }
                sink_emit(s, buf64, 16);
                fmt++;
                continue;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                sink_emit(s, str, strlen(str));
                fmt++;
                continue;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                sink_emit(s, &c, 1);
                fmt++;
                continue;
            }
            case '%': {
                sink_emit(s, "%", 1);
                fmt++;
                continue;
            }
            case '\0': {
                // '%' no fim da string, sem especificador depois —
                // formato malformado; emite o '%' literal e para,
                // em vez de ler além do fim da string por engano.
                sink_emit(s, "%", 1);
                return;
            }
            default: {
                // Especificador desconhecido (ex.: "%q"): emite os
                // dois caracteres literalmente, em vez de silenciar
                // o erro OU travar — é mais fácil perceber um typo de
                // formato assim do que com saída silenciosamente
                // incompleta.
                char unknown[2] = { '%', *fmt };
                sink_emit(s, unknown, 2);
                fmt++;
                continue;
            }
        }

        // ---- monta o campo numérico com sinal + padding, e emite ----
        int total_len = numlen + (is_negative ? 1 : 0);
        int pad = width > total_len ? width - total_len : 0;

        if (pad > 0 && !zero_pad) {
            // Padding com espaços vem ANTES do sinal (alinhamento à
            // direita "  -42", não "-  42").
            for (int i = 0; i < pad; i++) sink_emit(s, " ", 1);
        }
        if (is_negative) sink_emit(s, "-", 1);
        if (pad > 0 && zero_pad) {
            // Padding com zero vem DEPOIS do sinal ("-0042", não
            // "00-42") — comportamento padrão de printf.
            for (int i = 0; i < pad; i++) sink_emit(s, "0", 1);
        }
        sink_emit(s, numbuf, (size_t)numlen);

        fmt++;
    }
}

// ---- sink para printf/vprintf: escreve direto via write() ----
//
// PRINTF_MAX_LEN limita quanto uma única chamada de printf/vprintf
// pode escrever de uma vez — não há buffer dinâmico aqui (sem
// malloc envolvido de propósito, para printf funcionar mesmo antes
// de qualquer heap estar configurada, e para não arriscar uma
// mensagem de log gigante consumir memória sem limite). Mensagens
// mais longas que isso são truncadas silenciosamente; use múltiplas
// chamadas de printf() se precisar escrever muito texto.
#define PRINTF_MAX_LEN 512

typedef struct {
    char buf[PRINTF_MAX_LEN];
    size_t len;
} write_sink_ctx_t;

static void write_sink(void* ctx, const char* data, size_t len) {
    write_sink_ctx_t* wc = (write_sink_ctx_t*)ctx;
    size_t remaining = PRINTF_MAX_LEN - wc->len;
    size_t to_copy = len < remaining ? len : remaining;
    memcpy(wc->buf + wc->len, data, to_copy);
    wc->len += to_copy;
}

int vprintf(const char* fmt, va_list args) {
    write_sink_ctx_t ctx;
    ctx.len = 0;

    sink_t s;
    s.emit = write_sink;
    s.ctx = &ctx;
    s.total_written = 0;

    format_core(&s, fmt, args);

    write(ctx.buf, ctx.len);
    return (int)ctx.len; // retorna o que foi de fato escrito (pode
                           // ser menor que s.total_written se a
                           // formatação excedeu PRINTF_MAX_LEN)
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
}

int puts(const char* s) {
    size_t len = strlen(s);
    write(s, len);
    write("\n", 1);
    return (int)(len + 1);
}

// ---- sink para snprintf/vsnprintf: escreve num buffer do chamador ----
typedef struct {
    char* buf;
    size_t size;   // capacidade TOTAL de buf, incluindo o '\0' final
    size_t pos;    // posição atual de escrita (pode passar de `size`
                    // internamente — é assim que sabemos quanto
                    // TERIA sido escrito, para o retorno C99)
} buf_sink_ctx_t;

static void buf_sink(void* ctx, const char* data, size_t len) {
    buf_sink_ctx_t* bc = (buf_sink_ctx_t*)ctx;
    for (size_t i = 0; i < len; i++) {
        if (bc->pos + 1 < bc->size) { // deixa espaço para o '\0' final
            bc->buf[bc->pos] = data[i];
        }
        bc->pos++;
    }
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    buf_sink_ctx_t ctx;
    ctx.buf  = buf;
    ctx.size = size;
    ctx.pos  = 0;

    sink_t s;
    s.emit = buf_sink;
    s.ctx = &ctx;
    s.total_written = 0;

    format_core(&s, fmt, args);

    if (size > 0) {
        size_t term_pos = ctx.pos < size - 1 ? ctx.pos : size - 1;
        buf[term_pos] = '\0';
    }
    return (int)ctx.pos; // convenção C99: quanto TERIA sido escrito
                           // (sem contar o '\0') se o buffer fosse
                           // grande o suficiente — permite ao
                           // chamador detectar truncamento comparando
                           // com `size`.
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}
