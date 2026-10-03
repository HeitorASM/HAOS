# libc/ — biblioteca C mínima do HAOS

Substitui a antiga pasta `userland/` (removida). Em vez de escrever
programas de teste inteiramente em Assembly chamando `syscall` na
mão, agora é possível escrever programas HEXEC em **C de verdade**,
usando esta libc mínima e freestanding.

## O que já existe

- **`unistd.h`**:
  - E/S: `write()`, `read()`, `open()`, `open2()`, `close()`, `write_fd()`, `lseek()`
  - Processos: `spawn()`, `waitpid()`, `getpid()`, `yield()`, `_exit()`
  - Tempo: `uptime_ms()`, `sleep_ms()` (resolução de 10 ms)
  - Sistema de arquivos: `stat()`, `readdir()`, `getcwd()`, `chdir()`,
    `mkdir()`, `unlink()` — caminhos relativos são resolvidos a partir
    do diretório de trabalho **do processo** (herdado do terminal/pai)
  - Memória: `sbrk()`
  - Estruturas (`struct haos_stat`, `struct haos_dirent`): `haos/fs.h`
- **`haos/gfx.h`**: janelas gráficas para programas Ring 3 —
  `win_create()`, `win_destroy()`, `win_size()`, `win_fill_rect()`,
  `win_text()`/`win_text_bg()`, `win_blit()` e o laço de eventos
  `win_event()` (teclado, mouse, redimensionamento). O programa desenha
  num canvas que pertence ao kernel; o WM só o exibe dentro da moldura.
  `win_event()` devolve `-1` quando o usuário fecha a janela (X) — é o
  sinal para o programa encerrar. Exemplo: `examples/winpaint/`.
- **`stdlib.h`**: `exit()`, `malloc()`, `free()`, `realloc()`
- **`string.h`**: `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`,
  `strcpy`, `strncpy`, `strcat`, `strcmp`, `strncmp`
- **`stdio.h`**: `printf()`, `vprintf()`, `snprintf()`, `vsnprintf()`, `puts()`
  — suporta `%d %i %u %x %X %s %c %p %%`, largura e zero-padding
  (`%5d`, `%04x`); NÃO suporta ponto flutuante (`%f`) nem
  modificadores de tamanho (`%ld`) — ver comentário completo em
  `include/stdio.h`
- **argc/argv**: `main(int argc, char** argv)` — o loader (`process_create`)
  monta os argumentos na pilha de usuário antes de saltar para
  `_start`, seguindo a mesma convenção do `_start` do Linux x86_64;
  o comando `run` do terminal aceita `run programa.hexec arg1 arg2`

## O que NÃO existe ainda

- Descritores de arquivo padrão (stdin/stdout/stderr como fds 0/1/2
  de verdade) — `write()` sempre vai para o canal de stdout implícito
  do processo (redirecionado para o terminal que o lançou), não para
  um "fd 1"; fd 0 só serve para `read()` de stdin (não para `lseek()`)
- Pipes e redirecionamento (`>`, `|`)
- Renomear/mover arquivos, permissões, timestamps
- `fork()`/`exec()` — o modelo é `spawn()` + `waitpid()`
- Sleep sem polling: `sleep_ms()` cede a CPU em laço até o prazo (a
  task continua escalonável); um bloqueio com "wake_tick" no
  scheduler é melhoria futura

## Como compilar a libc

```bash
cd libc
make clean
make all
```

Isso gera:
- `build/crt0.o` — o ponto de entrada (`_start`), linkado
  **separadamente** por cada programa (não entra em `libc.a`)
- `build/libc.a` — todo o resto (syscall, unistd, string, stdlib)

## Como escrever e compilar um novo programa HEXEC em C

1. Escreva seu `main.c` normalmente, incluindo só os headers desta
   libc (`unistd.h`, `string.h`, etc. — nunca a libc do seu sistema
   host). Veja `examples/hello_c/main.c` como referência mínima.

2. Compile para objeto, apontando para os headers da libc:
   ```bash
   x86_64-linux-gnu-gcc -ffreestanding -fno-stack-protector -fno-pic \
       -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
       -nostdlib -nodefaultlibs -O2 -Wall -Wextra -std=c11 \
       -I libc/include -m64 \
       -c meuprograma.c -o meuprograma.o
   ```

3. Linke contra `crt0.o` + `libc.a`, usando o linker script
   `hexec.ld` (garante que tudo carrega no endereço certo,
   `HEXEC_LOAD_ADDR` — ver `kernel/hexec.h`):
   ```bash
   x86_64-linux-gnu-ld -T libc/hexec.ld -o meuprograma.elf \
       libc/build/crt0.o meuprograma.o \
       -L libc/build -lc
   ```
   **A ordem importa**: `crt0.o` sempre primeiro, para que `_start`
   fique no offset 0 (ver comentário em `hexec.ld`).

4. Empacote o ELF resultante como `.hexec`:
   ```bash
   python3 libc/mkhexec.py meuprograma.elf meuprograma.hexec
   ```
   Isso extrai `code_size` (via `objcopy -O binary`) e `bss_size`
   (via `size`) automaticamente — diferente da versão anterior deste
   script, que exigia esses valores como argumentos manuais para
   binários "flat" sem informação de seções (a era do
   `userland/hello.asm`, agora removida).

5. Rode no HAOS: copie o `.hexec` para dentro do VFS (hoje, o único
   jeito prático é embutir como array C em
   `kernel/generated/` e integrar em `fs/vfs.c` — ver
   `kernel/generated/hello_hexec_data.h` como referência; um
   mecanismo melhor de "instalar" um `.hexec` no VFS sem recompilar o
   kernel é trabalho futuro) e execute com `run meuprograma.hexec`
   no terminal, ou dando duplo-clique nele no explorador de arquivos.

## Por que a libc é linkada em C separado do kernel

O kernel (pasta `kernel/`) roda em **Ring 0**; qualquer coisa
linkada contra esta libc roda em **Ring 3**, dentro de um processo
isolado (ver Fase 3 do scheduler: `paging_create_address_space`).
Mesmo que hoje os tipos básicos (`uint64_t`, etc.) sejam idênticos
aos de `kernel/types.h`, a libc tem seu próprio `haos/types.h`
independente — nunca inclua headers de `kernel/` num programa de
usuário. Essa fronteira existe mesmo parecendo redundante agora,
porque deixa claro, desde já, o que é código de kernel e o que é
código de aplicação — importante à medida que mais syscalls e mais
programas forem adicionados.
