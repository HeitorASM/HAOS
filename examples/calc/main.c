// examples/calc/main.c — calculadora interativa para o HAOS.
//
// Le expressoes simples do tipo "numero operador numero" (ex.:
// "3 + 5", "10 / 4", "2.5 * 3") da entrada padrao (stdin, syscall
// SYS_READ com fd=0 — ver kernel/syscall.c) e imprime o resultado,
// em loop, ate o usuario digitar "sair" ou "exit".
//
// ---- Por que aritmetica fixed-point em vez de double ----
//
// A libc do HAOS e compilada com -mno-sse -mno-sse2 (ver
// libc/Makefile) e processos de usuario rodam sem FPU/SSE
// inicializada — nao ha "double" de verdade disponivel em Ring 3
// aqui. printf() tambem nao suporta %f (ver stdio.h). Em vez de
// fingir suporte a ponto flutuante e gerar codigo que usaria
// instrucoes SSE por baixo dos panos (o que travaria o processo na
// primeira operacao), esta calculadora trabalha inteiramente com
// inteiros representando "centesimos" (fixed-point de 2 casas
// decimais, como se faz tradicionalmente para dinheiro): o numero
// "3.14" e armazenado como 314. Todas as quatro operacoes fazem a
// conversao de escala manualmente. Isso cobre bem o caso de uso
// "contas simples" sem precisar de FPU nenhuma.
//
// ---- Por que parsing manual em vez de scanf ----
//
// Esta libc nao tem scanf/atof (ver libc/README.md) — nem faria
// sentido tao cedo, dado que nao ha FILE* nem stdin como stream
// bufferizado. Em vez disso, lemos byte a byte via read(0, &c, 1)
// ate um '\n' (ver read_line abaixo), e implementamos um parser
// bem pequeno e direto para o formato aceito.

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define LINE_MAX 128

// ---- Le uma linha do stdin, byte a byte ----
//
// read(0, buf, n) e BLOQUEANTE no HAOS (ver comentario em
// kernel/syscall.c, sys_read_stdin): se nao ha tecla disponivel
// ainda, a chamada so retorna quando o usuario digitar algo — nao
// ha necessidade de nenhum laco de espera ocupada aqui, o kernel
// cuida disso escalonando outras tasks enquanto este processo
// dorme. Le um byte de cada vez porque o terminal HAOS ainda nao
// tem um conceito de "linha completa pronta de uma vez" do lado do
// kernel (cada tecla vira uma escrita separada no stdin_ring) —
// ler byte a byte e a forma mais simples de montar uma linha a
// partir disso.
//
// Retorna o comprimento da linha (sem o '\n'), ou -1 se o stdin
// fechou (EOF — ver comentario em sys_read_stdin sobre quando isso
// acontece: terminal fechado com o processo ainda rodando).
static int read_line(char* buf, int max_len) {
    int len = 0;
    for (;;) {
        char c;
        ssize_t n = read(0, &c, 1);
        if (n <= 0) {
            // EOF ou erro: se ja tinha algo na linha, devolve o que
            // deu tempo de ler; senao sinaliza fim de entrada.
            return len > 0 ? len : -1;
        }
        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            return len;
        }
        if (c == '\b' || c == 0x08) {
            // O terminal ja trata backspace no eco visual dele (ver
            // gui/apps/terminal.cpp) sem enviar 0x08 para o stdin do
            // processo — mas tratamos aqui tambem por robustez, caso
            // esse comportamento mude no futuro ou outro front-end
            // decida repassar backspace cru.
            if (len > 0) len--;
            continue;
        }
        if (len < max_len - 1) {
            buf[len++] = c;
        }
        // bytes alem de max_len-1 sao descartados silenciosamente —
        // uma linha absurdamente longa nao deveria travar o parser.
    }
}

// ---- Parsing de numero decimal -> fixed-point (centesimos) ----
//
// Aceita um sinal opcional, parte inteira, e opcionalmente um ponto
// decimal seguido de ate 2 digitos (dígitos extras sao ignorados —
// suficiente para "contas simples"). Escreve o valor em *out
// (unidades de 1/100) e devolve um ponteiro para o primeiro
// caractere NAO consumido (util para o chamador seguir parseando o
// resto da linha), ou NULL se nao havia nenhum digito valido no
// inicio de `s`.
static const char* parse_fixed(const char* s, long* out) {
    while (*s == ' ') s++;

    int negative = 0;
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    if (*s < '0' || *s > '9') return NULL; // nem um digito — invalido

    long int_part = 0;
    while (*s >= '0' && *s <= '9') {
        int_part = int_part * 10 + (*s - '0');
        s++;
    }

    long frac_part = 0;
    if (*s == '.') {
        s++;
        int digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 2) {
            frac_part = frac_part * 10 + (*s - '0');
            digits++;
            s++;
        }
        while (digits < 2) { frac_part *= 10; digits++; } // "1.5" -> 50 centesimos
        while (*s >= '0' && *s <= '9') s++; // ignora digitos extras de precisao
    }

    long value = int_part * 100 + frac_part;
    *out = negative ? -value : value;
    return s;
}

// ---- Impressao de um valor fixed-point (centesimos -> "N.NN") ----
static void print_fixed(long v) {
    if (v < 0) {
        printf("-");
        v = -v;
    }
    long int_part = v / 100;
    long frac_part = v % 100;
    printf("%d.%02d", (int)int_part, (int)frac_part);
}

// ---- Comandos especiais ----
static int is_exit_command(const char* line) {
    return strcmp(line, "sair") == 0 || strcmp(line, "exit") == 0 ||
           strcmp(line, "quit") == 0 || strcmp(line, "q") == 0;
}

static void print_help(void) {
    puts("Calculadora HAOS — contas simples de linha unica.");
    puts("Formato: <numero> <operador> <numero>");
    puts("Operadores: + - * /   (aceita numeros decimais, ex.: 2.5 * 4)");
    puts("Comandos: ajuda | sair");
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    puts("=== Calculadora HAOS ===");
    puts("Digite uma conta (ex.: 3 + 5) ou 'ajuda'. 'sair' para encerrar.");

    char line[LINE_MAX];

    for (;;) {
        printf("> ");

        int len = read_line(line, LINE_MAX);
        if (len < 0) {
            // EOF: o terminal que nos lancou nao existe mais (janela
            // fechada) — nao ha mais para quem mostrar nada, entao
            // so encerramos o processo.
            return 0;
        }
        if (len == 0) continue; // linha vazia: so mostra o prompt de novo

        if (is_exit_command(line)) {
            puts("Ate mais!");
            return 0;
        }
        if (strcmp(line, "ajuda") == 0 || strcmp(line, "help") == 0) {
            print_help();
            continue;
        }

        // ---- Parse: <numero> <op> <numero> ----
        long a = 0, b = 0;
        const char* p = parse_fixed(line, &a);
        if (!p) {
            puts("Erro: esperava um numero no inicio (ex.: 3 + 5). Digite 'ajuda'.");
            continue;
        }
        while (*p == ' ') p++;

        char op = *p;
        if (op != '+' && op != '-' && op != '*' && op != '/') {
            puts("Erro: operador invalido. Use um destes: + - * /");
            continue;
        }
        p++;

        const char* p2 = parse_fixed(p, &b);
        if (!p2) {
            puts("Erro: esperava um segundo numero (ex.: 3 + 5).");
            continue;
        }

        // ---- Calcula, com cuidado na escala fixed-point ----
        //
        // a e b estao em centesimos (ex.: 3.50 -> 350). Soma e
        // subtracao operam direto nessa escala (350 + 200 = 550,
        // que reimpresso vira "5.50" — certo). Multiplicacao
        // multiplicaria a escala junto (350 * 200 = 70000, que seria
        // "700.00" ao inves de "7.00") — por isso dividimos por 100
        // de volta depois de multiplicar. Divisao e o inverso: teria
        // que multiplicar por 100 antes de dividir para nao perder
        // toda a parte fracionaria num "a/b" inteiro.
        long result;
        int div_by_zero = 0;

        switch (op) {
            case '+':
                result = a + b;
                break;
            case '-':
                result = a - b;
                break;
            case '*':
                result = (a * b) / 100;
                break;
            case '/':
                if (b == 0) {
                    div_by_zero = 1;
                    result = 0;
                } else {
                    result = (a * 100) / b;
                }
                break;
            default:
                result = 0; // inalcancavel — op ja validado acima
                break;
        }

        if (div_by_zero) {
            puts("Erro: divisao por zero.");
            continue;
        }

        printf("= ");
        print_fixed(result);
        printf("\n");
    }
}
