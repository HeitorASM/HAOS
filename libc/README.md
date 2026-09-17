# libc/ — biblioteca C mínima do HAOS

Em vez de escrever programas de teste inteiramente em Assembly ou progrmas imbutidos
no OS chamando `syscall` na mão, agora
 é possível escrever programas HEXEC em **C de verdade**,
usando esta libc mínima e freestanding.

## O que já existe

- **`unistd.h`**: `write()`, `_exit()`
- **`stdlib.h`**: `exit()`
- **`string.h`**: `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`,
  `strcpy`, `strncpy`, `strcat`, `strcmp`, `strncmp`
- **`stdio.h`**: vazio por enquanto (stub) — `printf`/`puts` ficam
  para uma próxima iteração, quando fizer sentido investir num
  formatador de string

## O que NÃO existe ainda

- `malloc`/`free` — o kernel ainda não tem uma syscall de
  gerenciamento de memória de usuário (sbrk/mmap); até lá, um
  programa HEXEC só tem a memória que o loader já mapeou
  (código+dados+bss+pilha)
- `argc`/`argv` — o loader HEXEC não passa argumentos de linha de
  comando ainda (ver limitação documentada em `src/crt/crt0.asm`)
- Múltiplos arquivos abertos / descritores de arquivo — `write()`
  sempre vai para o stdout implícito do processo (redirecionado
  para o terminal que o lançou)

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
